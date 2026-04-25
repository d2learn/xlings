export module xlings.libs.tinyhttps;

import std;
import mcpplibs.tinyhttps;
import xlings.core.log;

export namespace xlings::tinyhttps {

// ── Public types ─────────────────────────────────────────────────────

struct DownloadOptions {
    std::filesystem::path destFile;
    std::vector<std::string> urls;          // primary + fallbacks, tried in order
    int retryCount        { 3 };
    int connectTimeoutSec { 30 };
    int maxTimeSec        { 600 };
    std::function<void(double total, double now)> onProgress;
    std::function<bool()> isCancelled;      // returns true to abort download
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
std::int64_t query_content_length(const std::string& url, int connectTimeoutSec = 10);

// Returns the proxy URL that would be used for `url` (libcurl-style env
// resolution: HTTPS_PROXY / HTTP_PROXY / ALL_PROXY with NO_PROXY exemption,
// case-insensitive variants accepted). Empty result means direct connection.
// Exposed for testability and for callers that want to display "what proxy
// am I about to use" — the actual download path calls this internally.
std::string resolve_proxy(std::string_view url);

// ── Implementations ──────────────────────────────────────────────────

namespace detail_ {

// Pull host out of an http(s) URL: "https://example.com:443/foo" → "example.com".
// Used by NO_PROXY suffix matching.
std::string url_host_(std::string_view url) {
    auto s = std::string{url};
    if (auto p = s.find("://"); p != std::string::npos) s = s.substr(p + 3);
    if (auto p = s.find('/'); p != std::string::npos) s = s.substr(0, p);
    if (auto p = s.rfind(':'); p != std::string::npos) {
        // Don't lop off a colon that's actually IPv6 part — but proxy-via-env
        // for IPv6 is enough of an edge case to ignore here.
        s = s.substr(0, p);
    }
    return s;
}

// libcurl/curl/Go-net compatible NO_PROXY matcher.
// `np` is comma-separated entries; an entry matches `host` if:
//   - exact equal
//   - leading dot suffix match (`.example.com` matches `foo.example.com` and `example.com`)
//   - bare suffix (`example.com` matches `foo.example.com` and `example.com`)
//   - `*` matches everything (rarely used; libcurl rejects it but Go honours it)
bool host_in_no_proxy_(std::string_view host, std::string_view np) {
    std::size_t i = 0;
    while (i < np.size()) {
        auto end = np.find(',', i);
        auto entry = np.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i);
        i = (end == std::string_view::npos) ? np.size() : end + 1;
        // trim spaces
        while (!entry.empty() && (entry.front() == ' ' || entry.front() == '\t')) entry.remove_prefix(1);
        while (!entry.empty() && (entry.back()  == ' ' || entry.back()  == '\t')) entry.remove_suffix(1);
        if (entry.empty()) continue;
        if (entry == "*") return true;
        if (entry.front() == '.') entry.remove_prefix(1);
        if (host == entry) return true;
        if (host.size() > entry.size()
            && host[host.size() - entry.size() - 1] == '.'
            && host.substr(host.size() - entry.size()) == entry) {
            return true;
        }
    }
    return false;
}

// Resolve which proxy to use for `url` from env, libcurl-style. Returns empty
// string when direct connection should be used. Matches libcurl/Go/git
// behaviour: HTTPS_PROXY for https:// URLs, HTTP_PROXY for http://, ALL_PROXY
// as fallback, NO_PROXY exemptions, lowercase variants accepted.
std::string env_proxy_for_(std::string_view url) {
    auto get = [](const char* name) -> const char* {
        const char* v = std::getenv(name);
        return (v && *v) ? v : nullptr;
    };

    // NO_PROXY exemption first.
    if (auto np = get("NO_PROXY") ?: get("no_proxy")) {
        if (host_in_no_proxy_(url_host_(url), np)) return {};
    }

    bool isHttps = url.starts_with("https://");
    if (isHttps) {
        if (auto p = get("HTTPS_PROXY") ?: get("https_proxy")) return p;
    } else {
        if (auto p = get("HTTP_PROXY") ?: get("http_proxy")) return p;
    }
    if (auto p = get("ALL_PROXY") ?: get("all_proxy")) return p;
    return {};
}

auto make_client(int connectTimeoutSec, int readTimeoutSec, std::string_view url)
    -> mcpplibs::tinyhttps::HttpClient {
    mcpplibs::tinyhttps::HttpClientConfig cfg;
    cfg.connectTimeoutMs = connectTimeoutSec * 1000;
    cfg.readTimeoutMs = readTimeoutSec * 1000;
    cfg.verifySsl = true;
    cfg.keepAlive = false;
    cfg.maxRedirects = 10;
    if (auto proxy = env_proxy_for_(url); !proxy.empty()) {
        log::debug("tinyhttps: using proxy {} for {}", proxy, url);
        cfg.proxy = std::move(proxy);
    }
    return mcpplibs::tinyhttps::HttpClient(std::move(cfg));
}

// Single download attempt: stream GET url → dest file with progress + cancel
DownloadFileResult download_once(
    const std::string& url,
    const std::filesystem::path& dest,
    int connectSec,
    int maxSec,
    std::function<void(double, double)> onProgress,
    std::function<bool()> isCancelled = nullptr
) {
    auto client = make_client(connectSec, maxSec, url);

    mcpplibs::tinyhttps::DownloadProgressFn progress;
    if (onProgress) {
        progress = [&](std::int64_t total, std::int64_t downloaded) {
            onProgress(static_cast<double>(total), static_cast<double>(downloaded));
        };
    }

    auto result = client.download_to_file(url, dest, progress, isCancelled);

    if (!result.ok()) {
        return {false, result.error.empty()
            ? "HTTP " + std::to_string(result.statusCode) : result.error};
    }

    return {true, {}};
}

} // namespace detail_

std::string resolve_proxy(std::string_view url) {
    return detail_::env_proxy_for_(url);
}

void global_init() {
    mcpplibs::tinyhttps::Socket::platform_init();
}

void global_cleanup() {
    mcpplibs::tinyhttps::Socket::platform_cleanup();
}

DownloadFileResult download_file(const DownloadOptions& opts) {
    global_init();
    if (opts.urls.empty()) return {false, "no URLs provided"};

    std::error_code ec;
    std::filesystem::create_directories(opts.destFile.parent_path(), ec);

    std::string lastErr;
    for (auto& url : opts.urls) {
        if (opts.isCancelled && opts.isCancelled()) return {false, "cancelled"};
        for (int att = 0; att <= opts.retryCount; ++att) {
            if (opts.isCancelled && opts.isCancelled()) return {false, "cancelled"};
            auto r = detail_::download_once(url, opts.destFile,
                opts.connectTimeoutSec, opts.maxTimeSec,
                opts.onProgress, opts.isCancelled);
            if (r.success) return r;
            lastErr = r.error;
            std::filesystem::remove(opts.destFile, ec);
            if (att < opts.retryCount) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(500 * (att + 1)));
            }
        }
    }
    return {false, lastErr};
}

double probe_latency(const std::string& url, int timeoutMs) {
    global_init();
    mcpplibs::tinyhttps::Socket sock;
    // Extract host and port from URL
    std::string rest = url;
    auto sep = rest.find("://");
    if (sep != std::string::npos) rest = rest.substr(sep + 3);
    auto slash = rest.find('/');
    if (slash != std::string::npos) rest = rest.substr(0, slash);

    int port = url.find("https") != std::string::npos ? 443 : 80;
    std::string host = rest;
    auto colon = rest.rfind(':');
    if (colon != std::string::npos) {
        host = rest.substr(0, colon);
        try { port = std::stoi(rest.substr(colon + 1)); } catch (...) {}
    }

    auto t0 = std::chrono::steady_clock::now();
    if (!sock.connect(host.c_str(), port, timeoutMs)) {
        return std::numeric_limits<double>::infinity();
    }
    sock.close();
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

std::int64_t query_content_length(const std::string& url, int connectTimeoutSec) {
    global_init();
    auto client = detail_::make_client(connectTimeoutSec, /*readTimeoutSec=*/60, url);

    mcpplibs::tinyhttps::HttpRequest req;
    req.method = mcpplibs::tinyhttps::Method::HEAD;
    req.url = url;
    req.headers["User-Agent"] = "xlings/1.0";

    auto resp = client.send(req);

    if (!resp.ok()) return -1;

    // Case-insensitive content-length lookup
    for (auto& [k, v] : resp.headers) {
        std::string lower = k;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "content-length") {
            try { return std::stoll(v); } catch (...) { return -1; }
        }
    }
    return -1;
}

} // namespace xlings::tinyhttps
