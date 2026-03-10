module;

#include <cstdio>
#include <cstring>

// mbedTLS
#include <mbedtls/build_info.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#ifdef MBEDTLS_USE_PSA_CRYPTO
#include <psa/crypto.h>
#endif

// Platform sockets
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#define poll WSAPoll
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

export module xlings.tinyhttps;

import std;

export namespace xlings::tinyhttps {

// ── Public types (same interface as curl.cppm) ──────────────────────────

struct DownloadOptions {
    std::filesystem::path destFile;
    std::vector<std::string> urls;          // primary + fallbacks, tried in order
    int retryCount        { 3 };
    int connectTimeoutSec { 30 };
    int maxTimeSec        { 600 };
    std::function<void(double total, double now)> onProgress;
};

struct DownloadFileResult {
    bool success { false };
    std::string error;
};

void global_init();
void global_cleanup();
DownloadFileResult download_file(const DownloadOptions& opts);
double probe_latency(const std::string& url, int timeoutMs = 2000);
bool fetch_to_file(const std::string& url, const std::filesystem::path& dest);
// Query Content-Length via HEAD request (follows redirects). Returns -1 on failure.
int64_t query_content_length(const std::string& url, int connectTimeoutSec = 10);

// ── Internal detail ─────────────────────────────────────────────────────

namespace detail_ {

// --- Platform socket abstraction ---

#ifdef _WIN32
using sockfd_t = SOCKET;
constexpr sockfd_t BAD_SOCK = INVALID_SOCKET;
void sock_close(sockfd_t s) { closesocket(s); }
bool sock_would_block() {
    auto e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
void sock_set_nonblock(sockfd_t s) {
    unsigned long m = 1;
    ioctlsocket(s, FIONBIO, &m);
}
void sock_set_block(sockfd_t s) {
    unsigned long m = 0;
    ioctlsocket(s, FIONBIO, &m);
}
#else
using sockfd_t = int;
constexpr sockfd_t BAD_SOCK = -1;
void sock_close(sockfd_t s) { ::close(s); }
bool sock_would_block() {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
}
void sock_set_nonblock(sockfd_t s) {
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
}
void sock_set_block(sockfd_t s) {
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) & ~O_NONBLOCK);
}
#endif

// --- URL parser ---

struct ParsedUrl {
    std::string scheme, host, path;
    int port;
    bool tls;
};

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl r;
    std::string rest = url;

    auto sep = rest.find("://");
    if (sep != std::string::npos) {
        r.scheme = rest.substr(0, sep);
        rest = rest.substr(sep + 3);
    } else {
        r.scheme = "https";
    }
    r.tls = (r.scheme == "https");
    r.port = r.tls ? 443 : 80;

    auto slash = rest.find('/');
    if (slash != std::string::npos) {
        r.path = rest.substr(slash);
        rest = rest.substr(0, slash);
    } else {
        r.path = "/";
    }

    // Handle host:port (skip IPv6 bracket colons)
    auto colon = rest.rfind(':');
    if (colon != std::string::npos && rest.find('[') == std::string::npos) {
        r.host = rest.substr(0, colon);
        try { r.port = std::stoi(rest.substr(colon + 1)); } catch (...) {}
    } else {
        r.host = rest;
    }

    return r;
}

// --- CA certificates ---

std::string find_ca_bundle() {
    for (auto p : {"/etc/ssl/certs/ca-certificates.crt",
                   "/etc/pki/tls/certs/ca-bundle.crt",
                   "/etc/ssl/cert.pem"}) {
        if (std::filesystem::exists(p)) return p;
    }
    return {};
}

std::string tls_err(int ret) {
    char buf[128];
    mbedtls_strerror(ret, buf, sizeof(buf));
    return buf;
}

// --- mbedTLS BIO callbacks (operate on raw socket fd) ---

int bio_send(void* ctx, const unsigned char* buf, size_t len) {
    auto fd = *static_cast<sockfd_t*>(ctx);
    auto n = ::send(fd, reinterpret_cast<const char*>(buf), static_cast<int>(len), 0);
    if (n < 0) return sock_would_block() ? MBEDTLS_ERR_SSL_WANT_WRITE
                                         : MBEDTLS_ERR_NET_SEND_FAILED;
    return static_cast<int>(n);
}

int bio_recv_timeout(void* ctx, unsigned char* buf, size_t len, uint32_t timeout) {
    auto fd = *static_cast<sockfd_t*>(ctx);
    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, static_cast<int>(timeout));
    if (pr == 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    if (pr < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
    auto n = ::recv(fd, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
    if (n < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
    return static_cast<int>(n);
}

// --- TCP connect with timeout ---

sockfd_t tcp_connect(const std::string& host, int port, int timeoutMs) {
    auto portStr = std::to_string(port);
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res)
        return BAD_SOCK;

    sockfd_t fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == BAD_SOCK) { freeaddrinfo(res); return BAD_SOCK; }

    sock_set_nonblock(fd);
    int cr = ::connect(fd, res->ai_addr, static_cast<int>(res->ai_addrlen));
    freeaddrinfo(res);

    if (cr < 0 && !sock_would_block()) {
        sock_close(fd);
        return BAD_SOCK;
    }

    if (cr != 0) {
        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int pr = poll(&pfd, 1, timeoutMs);
        if (pr <= 0) { sock_close(fd); return BAD_SOCK; }

        int err = 0;
        auto errlen = static_cast<socklen_t>(sizeof(err));
        getsockopt(fd, SOL_SOCKET, SO_ERROR,
                   reinterpret_cast<char*>(&err), &errlen);
        if (err != 0) { sock_close(fd); return BAD_SOCK; }
    }

    sock_set_block(fd);
    return fd;
}

// --- TLS connection (RAII) ---

struct Conn {
    sockfd_t fd = BAD_SOCK;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt cacert;
    bool useTls = false;

    Conn() {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_x509_crt_init(&cacert);
    }

    ~Conn() {
        if (useTls) mbedtls_ssl_close_notify(&ssl);
        mbedtls_x509_crt_free(&cacert);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        if (fd != BAD_SOCK) sock_close(fd);
    }

    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    std::string open(const ParsedUrl& url, int timeoutMs) {
        fd = tcp_connect(url.host, url.port, timeoutMs);
        if (fd == BAD_SOCK) return "connect failed: " + url.host;

        if (!url.tls) return {};
        useTls = true;

        int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func,
                                         &entropy, nullptr, 0);
        if (ret != 0) return "rng seed: " + tls_err(ret);

        ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret != 0) return "ssl config: " + tls_err(ret);

        // mbedTLS 3.6.1 TLS 1.3 key derivation fails in static builds;
        // force TLS 1.2 which works reliably
        mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);

        auto ca = find_ca_bundle();
        if (!ca.empty()) {
            mbedtls_x509_crt_parse_file(&cacert, ca.c_str());
            mbedtls_ssl_conf_ca_chain(&conf, &cacert, nullptr);
            mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
        } else {
            mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
        }

        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
        mbedtls_ssl_conf_read_timeout(&conf, static_cast<uint32_t>(timeoutMs));

        ret = mbedtls_ssl_setup(&ssl, &conf);
        if (ret != 0) return "ssl setup: " + tls_err(ret);

        mbedtls_ssl_set_hostname(&ssl, url.host.c_str());
        mbedtls_ssl_set_bio(&ssl, &fd, bio_send, nullptr, bio_recv_timeout);

        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE)
                return "TLS handshake: " + tls_err(ret);
        }

        return {};
    }

    int send_all(const std::string& data) {
        auto* p = reinterpret_cast<const unsigned char*>(data.data());
        size_t left = data.size();
        while (left > 0) {
            int n;
            if (useTls) {
                n = mbedtls_ssl_write(&ssl, p, left);
            } else {
                n = static_cast<int>(
                    ::send(fd, reinterpret_cast<const char*>(p),
                           static_cast<int>(left), 0));
            }
            if (n < 0) {
                if (n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
                return n;
            }
            p += n;
            left -= static_cast<size_t>(n);
        }
        return 0;
    }

    int recv_some(unsigned char* buf, size_t len) {
        if (useTls)
            return mbedtls_ssl_read(&ssl, buf, len);
        auto n = ::recv(fd, reinterpret_cast<char*>(buf),
                        static_cast<int>(len), 0);
        return static_cast<int>(n);
    }
};

// --- HTTP response header ---

struct HttpResp {
    int status = 0;
    int64_t contentLen = -1;
    bool chunked = false;
    std::string location;
};

std::pair<HttpResp, std::vector<unsigned char>>
read_http_response(Conn& conn) {
    std::vector<unsigned char> buf;
    unsigned char tmp[4096];

    while (buf.size() < 65536) {
        int n = conn.recv_some(tmp, sizeof(tmp));
        if (n <= 0) break;
        buf.insert(buf.end(), tmp, tmp + n);

        // Search for \r\n\r\n
        if (buf.size() >= 4) {
            for (size_t i = 0; i + 3 < buf.size(); ++i) {
                if (buf[i] == '\r' && buf[i+1] == '\n' &&
                    buf[i+2] == '\r' && buf[i+3] == '\n') {

                    std::string hdr(buf.begin(), buf.begin() + i + 4);
                    std::vector<unsigned char> extra(buf.begin() + i + 4, buf.end());

                    HttpResp resp;
                    // Status line: HTTP/1.1 200 OK
                    auto sp1 = hdr.find(' ');
                    if (sp1 != std::string::npos) {
                        auto sp2 = hdr.find(' ', sp1 + 1);
                        auto code = hdr.substr(sp1 + 1, sp2 - sp1 - 1);
                        try { resp.status = std::stoi(code); } catch (...) {}
                    }

                    // Parse headers
                    std::istringstream ss(hdr);
                    std::string line;
                    std::getline(ss, line); // skip status line
                    while (std::getline(ss, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (line.empty()) break;
                        auto colon = line.find(':');
                        if (colon == std::string::npos) continue;
                        auto key = line.substr(0, colon);
                        auto val = line.substr(colon + 1);
                        while (!val.empty() && val[0] == ' ') val.erase(0, 1);
                        for (auto& c : key)
                            c = static_cast<char>(
                                std::tolower(static_cast<unsigned char>(c)));

                        if (key == "content-length") {
                            try { resp.contentLen = std::stoll(val); } catch (...) {}
                        } else if (key == "transfer-encoding" &&
                                   val.find("chunked") != std::string::npos) {
                            resp.chunked = true;
                        } else if (key == "location") {
                            resp.location = val;
                        }
                    }

                    return {resp, extra};
                }
            }
        }
    }

    return {{}, {}};
}

// --- Body readers ---

bool read_body_content_length(Conn& conn, std::vector<unsigned char>& initial,
                              std::FILE* fp, int64_t totalSize,
                              std::function<void(double,double)>& onProgress) {
    int64_t got = 0;
    double total = (totalSize > 0) ? static_cast<double>(totalSize) : 0.0;

    if (!initial.empty()) {
        std::fwrite(initial.data(), 1, initial.size(), fp);
        got += static_cast<int64_t>(initial.size());
        if (onProgress) onProgress(total, static_cast<double>(got));
    }

    unsigned char buf[8192];
    while (totalSize < 0 || got < totalSize) {
        int n = conn.recv_some(buf, sizeof(buf));
        if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
        if (n < 0) return false;
        std::fwrite(buf, 1, static_cast<size_t>(n), fp);
        got += n;
        if (onProgress) onProgress(total, static_cast<double>(got));
    }
    return true;
}

bool read_body_chunked(Conn& conn, std::vector<unsigned char>& initial,
                       std::FILE* fp, int64_t& totalGot,
                       std::function<void(double,double)>& onProgress) {
    std::string buf(initial.begin(), initial.end());
    totalGot = 0;

    auto fill_until_crlf = [&]() -> bool {
        while (buf.find("\r\n") == std::string::npos) {
            unsigned char tmp[4096];
            int n = conn.recv_some(tmp, sizeof(tmp));
            if (n <= 0) return false;
            buf.append(reinterpret_cast<char*>(tmp), static_cast<size_t>(n));
        }
        return true;
    };

    while (true) {
        if (!fill_until_crlf()) return false;
        auto crlf = buf.find("\r\n");
        int64_t chunkSz;
        try { chunkSz = std::stoll(buf.substr(0, crlf), nullptr, 16); }
        catch (...) { return false; }
        buf.erase(0, crlf + 2);

        if (chunkSz == 0) break;

        // Consume chunk data
        int64_t rem = chunkSz;
        if (!buf.empty()) {
            auto use = std::min(rem, static_cast<int64_t>(buf.size()));
            std::fwrite(buf.data(), 1, static_cast<size_t>(use), fp);
            totalGot += use;
            rem -= use;
            buf.erase(0, static_cast<size_t>(use));
        }
        while (rem > 0) {
            unsigned char tmp[8192];
            auto want = std::min(rem, static_cast<int64_t>(sizeof(tmp)));
            int n = conn.recv_some(tmp, static_cast<size_t>(want));
            if (n <= 0) return false;
            std::fwrite(tmp, 1, static_cast<size_t>(n), fp);
            totalGot += n;
            rem -= n;
            if (onProgress) onProgress(0, static_cast<double>(totalGot));
        }

        // Consume trailing \r\n
        while (buf.size() < 2) {
            unsigned char tmp[4];
            int n = conn.recv_some(tmp, sizeof(tmp));
            if (n <= 0) return false;
            buf.append(reinterpret_cast<char*>(tmp), static_cast<size_t>(n));
        }
        buf.erase(0, 2);
    }

    return true;
}

// --- URL redirect resolution ---

std::string resolve_redirect(const std::string& base, const std::string& loc) {
    if (loc.find("://") != std::string::npos) return loc;  // absolute
    auto u = parse_url(base);
    std::string origin = u.scheme + "://" + u.host;
    if (u.port != (u.tls ? 443 : 80))
        origin += ":" + std::to_string(u.port);
    if (loc.starts_with("/")) return origin + loc;
    auto slash = u.path.rfind('/');
    return origin + (slash != std::string::npos ? u.path.substr(0, slash + 1) : "/") + loc;
}

// --- Single download attempt (follows redirects) ---

DownloadFileResult
download_once(const std::string& url,
              const std::filesystem::path& dest,
              int connectSec, int /*maxSec*/,
              std::function<void(double,double)> onProgress) {
    auto current = url;

    for (int redir = 0; redir < 10; ++redir) {
        auto parsed = parse_url(current);

        Conn conn;
        auto err = conn.open(parsed, connectSec * 1000);
        if (!err.empty()) return {false, err};

        auto req = "GET " + parsed.path + " HTTP/1.1\r\n"
                 + "Host: " + parsed.host + "\r\n"
                 + "User-Agent: xlings/1.0\r\n"
                 + "Accept: */*\r\n"
                 + "Connection: close\r\n\r\n";

        if (conn.send_all(req) != 0)
            return {false, "send failed"};

        auto [resp, extra] = read_http_response(conn);

        if (resp.status >= 300 && resp.status < 400 && !resp.location.empty()) {
            current = resolve_redirect(current, resp.location);
            continue;
        }

        if (resp.status != 200)
            return {false, "HTTP " + std::to_string(resp.status)};

        std::FILE* fp = std::fopen(dest.string().c_str(), "wb");
        if (!fp) return {false, "cannot open: " + dest.string()};

        bool ok;
        if (resp.chunked) {
            int64_t got = 0;
            ok = read_body_chunked(conn, extra, fp, got, onProgress);
        } else {
            ok = read_body_content_length(conn, extra, fp,
                                          resp.contentLen, onProgress);
        }

        std::fclose(fp);
        if (!ok) {
            std::error_code ec;
            std::filesystem::remove(dest, ec);
            return {false, "read error"};
        }

        return {true, {}};
    }

    return {false, "too many redirects"};
}

} // namespace detail_

// ── Public API implementations ──────────────────────────────────────────

namespace detail_ {
    std::once_flag globalInitFlag_;
    void ensure_init_() {
        std::call_once(globalInitFlag_, [] {
#ifdef MBEDTLS_USE_PSA_CRYPTO
            psa_crypto_init();
#endif
#ifdef _WIN32
            WSADATA wsa;
            WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        });
    }
} // namespace detail_

void global_init()    { detail_::ensure_init_(); }
void global_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

DownloadFileResult download_file(const DownloadOptions& opts) {
    detail_::ensure_init_();
    namespace fs = std::filesystem;
    if (opts.urls.empty()) return {false, "no URLs provided"};

    std::error_code ec;
    fs::create_directories(opts.destFile.parent_path(), ec);

    std::string lastErr;
    for (auto& url : opts.urls) {
        for (int att = 0; att <= opts.retryCount; ++att) {
            auto r = detail_::download_once(url, opts.destFile,
                opts.connectTimeoutSec, opts.maxTimeSec, opts.onProgress);
            if (r.success) return r;
            lastErr = r.error;
            fs::remove(opts.destFile, ec);
            if (att < opts.retryCount)
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(500 * (att + 1)));
        }
    }
    return {false, lastErr};
}

double probe_latency(const std::string& url, int timeoutMs) {
    detail_::ensure_init_();
    auto parsed = detail_::parse_url(url);
    auto t0 = std::chrono::steady_clock::now();
    auto fd = detail_::tcp_connect(parsed.host, parsed.port, timeoutMs);
    if (fd == detail_::BAD_SOCK)
        return std::numeric_limits<double>::infinity();
    detail_::sock_close(fd);
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

bool fetch_to_file(const std::string& url, const std::filesystem::path& dest) {
    DownloadOptions o;
    o.destFile = dest;
    o.urls = {url};
    o.retryCount = 3;
    o.connectTimeoutSec = 30;
    o.maxTimeSec = 120;
    return download_file(o).success;
}

int64_t query_content_length(const std::string& url, int connectTimeoutSec) {
    detail_::ensure_init_();
    auto current = url;

    for (int redir = 0; redir < 10; ++redir) {
        auto parsed = detail_::parse_url(current);

        detail_::Conn conn;
        auto err = conn.open(parsed, connectTimeoutSec * 1000);
        if (!err.empty()) return -1;

        auto req = "HEAD " + parsed.path + " HTTP/1.1\r\n"
                 + "Host: " + parsed.host + "\r\n"
                 + "User-Agent: xlings/1.0\r\n"
                 + "Connection: close\r\n\r\n";

        if (conn.send_all(req) != 0) return -1;

        auto [resp, extra] = detail_::read_http_response(conn);

        if (resp.status >= 300 && resp.status < 400 && !resp.location.empty()) {
            current = detail_::resolve_redirect(current, resp.location);
            continue;
        }

        if (resp.status == 200) return resp.contentLen;
        return -1;
    }

    return -1;
}

} // namespace xlings::tinyhttps
