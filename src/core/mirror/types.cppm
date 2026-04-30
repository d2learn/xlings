// xlings.core.mirror.types — data types for the mirror fallback subsystem.
//
// All enums and structs live here. Keep this partition pure-data so the
// other partitions (forms / registry / expand) can depend on it without
// pulling in JSON parsing or platform code.

export module xlings.core.mirror.types;

import std;

export namespace xlings::mirror {

// What kind of asset a URL is requesting. Drives mirror filtering — not
// every mirror handles every asset class (e.g. jsDelivr is raw-only).
enum class ResourceType {
    Release,    // GitHub release tarball / asset binary
    Raw,        // raw.githubusercontent.com text/binary
    Archive,    // /archive/<ref>.tar.gz or codeload.github.com
    Git,        // git clone — caller MUST set this explicitly; URL alone
                // can't disambiguate git from web view
    Unknown,    // not a GitHub-family URL we know how to mirror
};

// URL-rewriting strategy. Each form is a pure function from
// (original URL, mirror.host) → mirrored URL. See forms.cppm.
enum class Form {
    // Prefix strategy: prepend mirror host as proxy, original URL kept intact.
    //   in:  https://github.com/x/y/releases/download/v1/asset.tar.gz
    //   out: https://ghfast.top/https://github.com/x/y/releases/download/v1/asset.tar.gz
    Prefix,

    // Host-replace strategy: replace github.com / raw.githubusercontent.com
    // with the mirror host, rewriting paths where needed.
    //   in:  https://github.com/x/y/releases/download/v1/asset.tar.gz
    //   out: https://kkgithub.com/x/y/releases/download/v1/asset.tar.gz
    //   in:  https://raw.githubusercontent.com/x/y/main/file.txt
    //   out: https://kkgithub.com/x/y/raw/main/file.txt
    HostReplace,

    // jsDelivr CDN strategy: only valid for raw.githubusercontent.com URLs;
    // rewrites to jsDelivr's gh/ path syntax.
    //   in:  https://raw.githubusercontent.com/x/y/<ref>/path/file
    //   out: https://cdn.jsdelivr.net/gh/x/y@<ref>/path/file
    JsDelivr,
};

// Process-wide policy for fallback behavior. Resolved once at first use
// from env XLINGS_MIRROR_FALLBACK > .xlings.json mirror_fallback > Auto.
enum class Mode {
    Auto,    // default: try original first, then fall back through mirrors
    Off,     // strict: only the original URL is returned (CI / debugging)
    Force,   // skip the original URL, try mirrors first (heavily restricted networks)
};

// One entry in the mirror list. Loaded from compiled-in JSON or from
// ~/.xlings/data/github-mirrors.json. Field names mirror the JSON schema.
struct Mirror {
    std::string name;             // identifier for logs/debug, e.g. "ghfast"
    std::string host;             // mirror hostname, e.g. "ghfast.top"
    Form        form;
    std::vector<ResourceType> supports;
    int         priority = 100;   // ascending; lower = tried first
    std::optional<std::size_t> limit_bytes;  // per-file size cap (jsDelivr 50 MB)
};

// Per-call options passed to expand(). All fields optional with sensible
// defaults so the typical caller can just write `mirror::expand(url)`.
struct ExpandOptions {
    // Asset class hint. If unset, expand() will run classify() on the URL.
    // git clone callers MUST set this to ResourceType::Git — URL alone
    // can't tell us whether a github.com/x/y URL is being cloned or fetched.
    std::optional<ResourceType> type;

    // Per-call mode override. Defaults to current_mode() (process-wide).
    // Tests use this to force a specific mode without touching globals.
    std::optional<Mode> mode;

    // Known file size in bytes. Used to filter mirrors with limit_bytes
    // smaller than the request (e.g. jsDelivr's 50 MB cap). 0 = unknown.
    std::size_t expected_size = 0;
};

} // namespace xlings::mirror
