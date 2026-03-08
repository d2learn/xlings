module;

#include <curl/curl.h>

export module xlings.curl;

import std;

export namespace xlings::curl {

namespace detail_ {
    std::once_flag initFlag;
    void ensure_init() {
        std::call_once(initFlag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    }
} // namespace detail_

void global_init()    { detail_::ensure_init(); }
void global_cleanup() { curl_global_cleanup(); }

// ── download_file ──────────────────────────────────────────────────────

struct DownloadOptions {
    std::filesystem::path destFile;
    std::vector<std::string> urls;          // primary + fallbacks, tried in order
    int retryCount        { 3 };
    int connectTimeoutSec { 30 };
    int maxTimeSec        { 600 };
    // Called periodically: (bytesTotal, bytesNow).
    // bytesTotal may be 0 if the server doesn't send Content-Length.
    std::function<void(double total, double now)> onProgress;
};

struct DownloadFileResult {
    bool success { false };
    std::string error;
};

namespace detail_ {

struct WriteCtx {
    std::FILE* fp { nullptr };
};

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<WriteCtx*>(userdata);
    return std::fwrite(ptr, size, nmemb, ctx->fp);
}

struct ProgressCtx {
    std::function<void(double, double)>* fn { nullptr };
};

int progress_cb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* ctx = static_cast<ProgressCtx*>(clientp);
    if (ctx->fn && *ctx->fn)
        (*ctx->fn)(static_cast<double>(dltotal), static_cast<double>(dlnow));
    return 0;  // 0 = continue, non-zero = abort
}

} // namespace detail_

DownloadFileResult download_file(const DownloadOptions& opts) {
    namespace fs = std::filesystem;
    detail_::ensure_init();

    if (opts.urls.empty())
        return { false, "no URLs provided" };

    // Ensure parent directory exists
    std::error_code ec;
    fs::create_directories(opts.destFile.parent_path(), ec);
    if (ec) return { false, "mkdir failed: " + ec.message() };

    std::string lastError;

    for (auto& url : opts.urls) {
        // Retry loop per URL
        for (int attempt = 0; attempt <= opts.retryCount; ++attempt) {
            CURL* curl = curl_easy_init();
            if (!curl) { lastError = "curl_easy_init failed"; continue; }

            detail_::WriteCtx wctx;
            wctx.fp = std::fopen(opts.destFile.string().c_str(), "wb");
            if (!wctx.fp) {
                curl_easy_cleanup(curl);
                return { false, "cannot open file: " + opts.destFile.string() };
            }

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(opts.connectTimeoutSec));
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(opts.maxTimeSec));
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, detail_::write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wctx);

            // Progress callback
            detail_::ProgressCtx pctx;
            auto fnCopy = opts.onProgress;  // local copy for lifetime
            pctx.fn = &fnCopy;
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, detail_::progress_cb);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pctx);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

            auto res = curl_easy_perform(curl);
            std::fclose(wctx.fp);
            curl_easy_cleanup(curl);

            if (res == CURLE_OK) {
                return { true, {} };
            }

            lastError = curl_easy_strerror(res);
            // Remove partial file on failure
            fs::remove(opts.destFile, ec);

            if (attempt < opts.retryCount) {
                // Brief pause before retry
                std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
            }
        }
        // This URL exhausted retries, try next fallback
    }

    return { false, lastError };
}

// ── probe_latency ──────────────────────────────────────────────────────

// Send HEAD request, return connection time in seconds. Returns infinity on failure.
double probe_latency(const std::string& url, int timeoutMs = 2000) {
    detail_::ensure_init();
    CURL* curl = curl_easy_init();
    if (!curl) return std::numeric_limits<double>::infinity();

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);           // HEAD request
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeoutMs));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
    // Suppress output
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
        +[](char*, size_t s, size_t n, void*) -> size_t { return s * n; });

    auto res = curl_easy_perform(curl);
    double latency = std::numeric_limits<double>::infinity();
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &latency);
    }
    curl_easy_cleanup(curl);
    return latency;
}

// ── fetch_to_file ──────────────────────────────────────────────────────

// Simple GET download. Returns true on success.
bool fetch_to_file(const std::string& url, const std::filesystem::path& dest) {
    detail_::ensure_init();
    DownloadOptions opts;
    opts.destFile = dest;
    opts.urls = { url };
    opts.retryCount = 3;
    opts.connectTimeoutSec = 30;
    opts.maxTimeSec = 120;
    return download_file(opts).success;
}

} // namespace xlings::curl
