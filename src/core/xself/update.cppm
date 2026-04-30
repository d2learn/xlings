export module xlings.core.xself.update;

import std;

import xlings.core.log;
import xlings.platform;

namespace xlings::xself {

// `xlings self update` — refresh the package index and install/activate the
// latest xlings. Implemented by spawning `xlings` as a subprocess via
// platform::exec rather than calling cmd_install/cmd_use directly: that
// avoids the circular module dependency that would otherwise arise
// (xim.commands and xvm.commands both import xlings.core.xself).
export int cmd_update() {
    log::info("updating package index...");
    int rc = platform::exec("xlings update");
    if (rc != 0) {
        log::error("failed to update package index");
        return rc;
    }

    log::info("installing xlings@latest...");
    rc = platform::exec("xlings install xlings@latest -y");
    if (rc != 0) {
        log::warn("xlings package not available or install failed, skipping");
    } else {
        platform::exec("xlings use xlings latest");
    }

    return 0;
}

} // namespace xlings::xself
