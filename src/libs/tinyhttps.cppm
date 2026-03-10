export module xlings.libs.tinyhttps;

import std;
import mcpplibs.tinyhttps;

export namespace xlings::tinyhttps {

// ── Public types ─────────────────────────────────────────────────────

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
std::int64_t query_content_length(const std::string& url, int connectTimeoutSec = 10);

// ── Implementations ──────────────────────────────────────────────────

namespace detail_ {

auto make_client(int connectTimeoutSec, int readTimeoutSec = 60)
    -> mcpplibs::tinyhttps::HttpClient {
    mcpplibs::tinyhttps::HttpClientConfig cfg;
    cfg.connectTimeoutMs = connectTimeoutSec * 1000;
    cfg.readTimeoutMs = readTimeoutSec * 1000;
    cfg.verifySsl = true;
    cfg.keepAlive = false;
    return mcpplibs::tinyhttps::HttpClient(std::move(cfg));
}

// Single download attempt: GET url → dest file, with progress callback
DownloadFileResult download_once(
    const std::string& url,
    const std::filesystem::path& dest,
    int connectSec,
    int maxSec,
    std::function<void(double, double)> onProgress
) {
    auto client = make_client(connectSec, maxSec);

    mcpplibs::tinyhttps::HttpRequest req;
    req.method = mcpplibs::tinyhttps::Method::GET;
    req.url = url;
    req.headers["User-Agent"] = "xlings/1.0";
    req.headers["Accept"] = "*/*";

    auto resp = client.send(req);

    if (!resp.ok()) {
        return {false, "HTTP " + std::to_string(resp.statusCode) + " " + resp.statusText};
    }

    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);

    std::ofstream ofs(dest, std::ios::binary);
    if (!ofs) return {false, "cannot open: " + dest.string()};

    ofs.write(resp.body.data(), static_cast<std::streamsize>(resp.body.size()));
    ofs.close();

    if (onProgress) {
        auto total = static_cast<double>(resp.body.size());
        onProgress(total, total);
    }

    return {true, {}};
}

} // namespace detail_

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
        for (int att = 0; att <= opts.retryCount; ++att) {
            auto r = detail_::download_once(url, opts.destFile,
                opts.connectTimeoutSec, opts.maxTimeSec, opts.onProgress);
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
    // Parse host:port from URL to do a TCP connect timing
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
    auto client = detail_::make_client(connectTimeoutSec);

    mcpplibs::tinyhttps::HttpRequest req;
    req.method = mcpplibs::tinyhttps::Method::HEAD;
    req.url = url;
    req.headers["User-Agent"] = "xlings/1.0";

    auto resp = client.send(req);

    if (!resp.ok()) return -1;

    auto it = resp.headers.find("content-length");
    if (it == resp.headers.end()) {
        // Try case-insensitive lookup
        for (auto& [k, v] : resp.headers) {
            std::string lower = k;
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower == "content-length") {
                try { return std::stoll(v); } catch (...) { return -1; }
            }
        }
        return -1;
    }
    try { return std::stoll(it->second); } catch (...) { return -1; }
}

} // namespace xlings::tinyhttps
