// xlings.core.xself — top-level module entry for the `xlings self ...`
// command family.
//
// All actual command implementations live in partition files under
// src/core/xself/. This file's only job is to (a) re-export the public
// surface from those partitions and (b) route subcommand names to them.
//
// Layout:
//   xself.cppm                  — this file (router + help)
//   xself/init.cppm             — home layout helpers + `self init`
//   xself/install.cppm          — `self install` (bootstrap from release tarball)
//   xself/update.cppm           — `self update`
//   xself/config.cppm           — `self config`
//   xself/clean.cppm            — `self clean [--dry-run]`
//   xself/migrate.cppm          — `self migrate`
//   xself/doctor.cppm           — `self doctor [--fix]`
//   xself/compat_0_4_8.cppm     — COMPAT(0.4.8 → drop in 0.6.0): legacy
//                                 alias migration. See its header comment
//                                 for the removal procedure.

export module xlings.core.xself;

import std;

export import xlings.core.xself.init;
export import xlings.core.xself.install;
export import xlings.core.xself.update;
export import xlings.core.xself.config;
export import xlings.core.xself.clean;
export import xlings.core.xself.migrate;
export import xlings.core.xself.doctor;
// COMPAT(0.4.8 → drop in 0.6.0): re-exported so external callers
// (main.cpp, xvm/commands.cppm, xim/installer.cppm) reach
// `xself::compat::*` through the umbrella module without depending
// on the partition file directly.
export import xlings.core.xself.compat_0_4_8;

import xlings.libs.json;
import xlings.runtime;

namespace xlings::xself {

static int cmd_help(EventStream& stream) {
    nlohmann::json payload;
    payload["name"] = "self";
    payload["description"] = "Manage xlings itself";
    payload["args"] = nlohmann::json::array();
    payload["opts"] = nlohmann::json::array({
        {{"name", "install"},  {"desc", "Install xlings from release package"}},
        {{"name", "init"},     {"desc", "Create home/data/subos dirs"}},
        {{"name", "update"},   {"desc", "Update index + install latest xlings"}},
        {{"name", "config"},   {"desc", "Show configuration details"}},
        {{"name", "clean"},    {"desc", "Remove cache + gc orphaned packages (--dry-run)"}},
        {{"name", "migrate"},  {"desc", "Migrate old layout to subos/default"}},
        {{"name", "doctor"},   {"desc", "Verify workspace/shim consistency (--fix to repair)"}},
    });
    stream.emit(DataEvent{"help", payload.dump()});
    return 0;
}

export int run(int argc, char* argv[], EventStream& stream) {
    std::string action = (argc >= 3) ? argv[2] : "help";
    if (action == "install") return cmd_install();
    if (action == "init")    return cmd_init();
    if (action == "update")  return cmd_update();
    if (action == "config")  return cmd_config(stream);
    if (action == "clean") {
        bool dryRun = argc >= 4 && std::string(argv[3]) == "--dry-run";
        return cmd_clean(dryRun);
    }
    if (action == "migrate") return cmd_migrate();
    if (action == "doctor") {
        bool fix = false;
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--fix") { fix = true; break; }
        }
        return cmd_doctor(stream, fix);
    }
    return cmd_help(stream);
}

} // namespace xlings::xself
