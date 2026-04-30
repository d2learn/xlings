export module xlings.core.xself.clean;

import std;

import xlings.core.config;
import xlings.core.log;
import xlings.core.profile;

namespace xlings::xself {

// `xlings self clean [--dry-run]` — remove the cache directory and run
// the profile-level GC of orphaned packages.
export int cmd_clean(bool dryRun) {
    namespace fs = std::filesystem;
    auto& p = Config::paths();

    auto cachedir = p.homeDir / ".xlings";
    if (fs::exists(cachedir) && fs::is_directory(cachedir)) {
        if (dryRun) {
            log::println("  would remove cache: {}", cachedir.string());
        } else {
            std::error_code ec;
            fs::remove_all(cachedir, ec);
            if (ec) {
                log::error("failed to remove {}: {}", cachedir.string(), ec.message());
                return 1;
            }
            log::debug("cleaned cache: {}", cachedir.string());
        }
    }

    profile::gc(p.homeDir, dryRun);

    if (!dryRun) log::info("clean ok");
    return 0;
}

} // namespace xlings::xself
