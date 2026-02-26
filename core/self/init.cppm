module;

#include <cstdio>

export module xlings.xself:init;

import std;

import xlings.config;
import xlings.platform;

namespace xlings::xself {

namespace fs = std::filesystem;

// Base shim names (always created)
inline constexpr std::array<std::string_view, 7> SHIM_NAMES_BASE = {
    "xvm-shim", "xlings", "xvm", "xim", "xinstall", "xsubos", "xself"
};

// Optional shims (created only when pkg_root/bin/<name> exists)
inline constexpr std::array<std::string_view, 1> SHIM_NAMES_OPTIONAL = {"xmake"};

export bool is_builtin_shim(std::string_view name) {
    for (auto n : SHIM_NAMES_BASE)
        if (n == name) return true;
    for (auto n : SHIM_NAMES_OPTIONAL)
        if (n == name) return true;
    return false;
}

export void ensure_subos_shims(const fs::path& target_bin_dir,
                               const fs::path& shim_src,
                               const fs::path& pkg_root) {
    if (!fs::exists(shim_src)) return;

    std::string ext = shim_src.extension().string();

    for (auto name : SHIM_NAMES_BASE) {
        auto dst = target_bin_dir / (std::string(name) + ext);
        std::error_code ec;
        fs::copy_file(shim_src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::println(stderr, "[xlings:self]: failed to copy shim {} - {}",
                dst.string(), ec.message());
        }
    }

    if (!pkg_root.empty()) {
        auto bin_dir = pkg_root / "bin";
        for (auto name : SHIM_NAMES_OPTIONAL) {
            auto opt_bin = bin_dir / (std::string(name) + ext);
            if (fs::exists(opt_bin)) {
                auto dst = target_bin_dir / (std::string(name) + ext);
                std::error_code ec;
                fs::copy_file(shim_src, dst, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    std::println(stderr, "[xlings:self]: failed to copy shim {} - {}",
                        dst.string(), ec.message());
                }
            }
        }
    }

    platform::make_files_executable(target_bin_dir);
}

} // namespace xlings::xself
