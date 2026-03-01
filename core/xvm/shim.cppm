module;

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

#include <cstdio>

export module xlings.xvm.shim;

import std;

import xlings.config;
import xlings.log;
import xlings.platform;
import xlings.xvm.types;
import xlings.xvm.db;

export namespace xlings::xvm {

// Check if a program name is the xlings binary itself (not a shim target)
bool is_xlings_binary(std::string_view name) {
    static constexpr std::array<std::string_view, 3> XLINGS_NAMES = {
        "xlings", "xim", "xvm"
    };
    for (auto n : XLINGS_NAMES) {
        if (n == name) return true;
    }
    return false;
}

// Extract basename from argv[0], stripping path and extension
std::string extract_program_name(const char* argv0) {
    auto p = std::filesystem::path(argv0);
    return p.stem().string();
}

// Resolve the real executable path for a shim target
std::filesystem::path resolve_executable(const std::string& program_name,
                                         const std::string& path,
                                         const std::string& xlings_home) {
    namespace fs = std::filesystem;

    auto expanded = expand_path(path, xlings_home);
    auto base = fs::path(expanded);

    // Try direct: path/program_name
    auto candidate1 = base / program_name;
    if (fs::exists(candidate1)) return candidate1;

    // Try: path/bin/program_name
    auto candidate2 = base / "bin" / program_name;
    if (fs::exists(candidate2)) return candidate2;

    return {};
}

// Set environment variables for a program before exec
void setup_envs(const VData& vdata,
                const std::string& resolved_path,
                const std::string& xlings_home) {
    // Set envs from VData
    for (auto& [key, value] : vdata.envs) {
        auto expanded = expand_path(value, xlings_home);
        // Append to existing env if it exists (PATH-style)
        auto existing = std::string(std::getenv(key.c_str()) ? std::getenv(key.c_str()) : "");
        if (!existing.empty()) {
            expanded = expanded + platform::PATH_SEPARATOR + existing;
        }
        platform::set_env_variable(key, expanded);
    }

    // Prepend resolved program's directory to PATH
    if (!resolved_path.empty()) {
        auto dir = std::filesystem::path(resolved_path).parent_path().string();
        auto existing_path = std::string(std::getenv("PATH") ? std::getenv("PATH") : "");
        auto bin_dir = Config::paths().binDir.string();
        std::string new_path;
        if (!dir.empty()) new_path = dir;
        if (!bin_dir.empty()) {
            if (!new_path.empty()) new_path += platform::PATH_SEPARATOR;
            new_path += bin_dir;
        }
        if (!existing_path.empty()) {
            if (!new_path.empty()) new_path += platform::PATH_SEPARATOR;
            new_path += existing_path;
        }
        platform::set_env_variable("PATH", new_path);
    }
}

// Main shim dispatch: called when argv[0] is a tool name (not xlings/xim)
int shim_dispatch(const std::string& program_name, int argc, char* argv[]) {
    auto& cfg = Config::paths();
    auto xlings_home = cfg.homeDir.string();

    // Get effective workspace (project > subos > global)
    auto workspace = Config::effective_workspace();

    // Look up active version for this program
    auto version = get_active_version(workspace, program_name);
    if (version.empty()) {
        std::println(stderr, "xlings: no version set for '{}'", program_name);
        std::println(stderr, "  hint: xlings use {} <version>", program_name);
        return 1;
    }

    // Resolve version (fuzzy match)
    auto& db = Config::versions();
    auto resolved_version = match_version(db, program_name, version);
    if (resolved_version.empty()) {
        std::println(stderr, "xlings: version '{}' not found for '{}'", version, program_name);
        auto all = get_all_versions(db, program_name);
        if (!all.empty()) {
            std::print(stderr, "  available:");
            for (auto& v : all) std::print(stderr, " {}", v);
            std::println(stderr, "");
        }
        return 1;
    }

    auto vdata = get_vdata(db, program_name, resolved_version);
    if (!vdata) {
        std::println(stderr, "xlings: no path info for {} {}", program_name, resolved_version);
        return 1;
    }

    // Determine actual program name to exec (check bindings)
    std::string exec_name = program_name;
    auto vinfo = get_vinfo(db, program_name);

    // Check if the program_name is actually a binding of another target
    // Walk all targets to see if program_name is a binding key
    std::string binding_target;
    for (auto& [target, info] : db) {
        auto bit = info.bindings.find(program_name);
        if (bit != info.bindings.end()) {
            // This program is a binding of 'target'
            auto ws_ver = get_active_version(workspace, target);
            if (!ws_ver.empty()) {
                auto rv = match_version(db, target, ws_ver);
                if (!rv.empty()) {
                    auto bvit = bit->second.find(rv);
                    if (bvit != bit->second.end()) {
                        binding_target = bvit->second;
                        // Use the parent target's vdata for path resolution
                        vdata = get_vdata(db, target, rv);
                        exec_name = binding_target;
                        break;
                    }
                }
            }
        }
    }

    // Resolve the executable path
    auto exe_path = resolve_executable(exec_name, vdata->path, xlings_home);
    if (exe_path.empty()) {
        std::println(stderr, "xlings: executable '{}' not found", exec_name);
        std::println(stderr, "  path: {}", expand_path(vdata->path, xlings_home));
        std::println(stderr, "  hint: install with xlings install {}@{}", program_name, resolved_version);
        return 1;
    }

    // Setup environment
    setup_envs(*vdata, exe_path.string(), xlings_home);

    // Build argv for execvp
    auto exe_str = exe_path.string();
    std::vector<const char*> new_argv;
    new_argv.push_back(exe_str.c_str());
    for (int i = 1; i < argc; ++i) {
        new_argv.push_back(argv[i]);
    }
    new_argv.push_back(nullptr);

#if defined(__linux__) || defined(__APPLE__)
    execvp(exe_path.c_str(), const_cast<char* const*>(new_argv.data()));
    // If execvp returns, it failed
    std::println(stderr, "xlings: failed to exec '{}'", exe_path.string());
    return 1;
#else
    // Fallback for platforms without execvp
    std::string cmd = "\"" + exe_path.string() + "\"";
    for (int i = 1; i < argc; ++i) {
        cmd += " \"";
        cmd += argv[i];
        cmd += "\"";
    }
    return platform::exec(cmd);
#endif
}

} // namespace xlings::xvm
