-- libarchive package override (project-private).
--
-- Why this file exists
-- --------------------
-- xmake-repo's `libarchive` package definition declares its compression
-- backend deps as:
--
--     add_deps("zlib", "bzip2", "lz4", "zstd", "lzma")
--
-- Four of those names match the cmake `find_package` names that
-- libarchive's CMakeLists.txt probes during build (`ZLIB`, `BZip2`,
-- `lz4`, `zstd`) — so the libarchive build wires them in correctly.
--
-- The fifth, `lzma`, does NOT. xmake-repo's `lzma` package is the
-- 7-Zip LZMA SDK (header `LzmaLib.h`, function `LzmaCompress`). What
-- libarchive's cmake actually probes is `find_package(LibLZMA)` —
-- xz-utils' liblzma (header `lzma.h`, function `lzma_code`). Different
-- API surface entirely. xrepo's package for that is `xz`.
--
-- Net effect of the upstream `add_deps("lzma")` line: cmake's
-- LibLZMA detection fails, libarchive falls back to its
-- `archive_read_support_filter_xz_external` path that fork-exec's the
-- system `xz` / `lzma` binary at runtime. Symptoms:
--   * Linux musl-static deployments that lack a host `xz` binary →
--     .tar.xz / .tar.lzma extraction fails at runtime.
--   * Windows builds → no system `xz.exe` → same failure.
--   * Even on hosts where it works, every .tar.xz extract is a
--     fork-exec round-trip (slow, temp-file IO).
--
-- Concrete xim-pkgindex packages affected today: node@* (linux uses
-- node-vXXX-linux-x64.tar.xz) and llvm (macos arm64 uses .tar.xz).
--
-- Override strategy
-- -----------------
-- Re-declare libarchive as `libarchive-xlings`, set_base("libarchive")
-- to inherit URL list / version table / etc., then:
--   * swap `lzma` → `xz` in the deps so the cmake find_package call
--     resolves to xrepo-built liblzma instead of falling back;
--   * pass explicit `-DENABLE_LZMA=ON` (and the other backends) so
--     cmake's auto-detection cannot silently disable any of them on a
--     stripped builder image.
--
-- Including this file from the project's root xmake.lua is enough;
-- xmake's `package()` block registers a private package alongside any
-- xmake-repo packages and `add_requires("libarchive-xlings ...")`
-- picks it up.
--
-- When this can be removed
-- ------------------------
-- After xmake-repo merges a fix that changes `add_deps("lzma")` to
-- `add_deps("xz")` in their libarchive package def. Track at:
--   https://github.com/xmake-io/xmake-repo/tree/dev/packages/l/libarchive

package("libarchive-xlings")

    set_base("libarchive")

    -- Pin the same versions xmake-repo ships, so set_base inherits the
    -- URL templates and we just need version → sha256 mappings.
    add_versions("3.8.7", "4b787cca6697a95c7725e45293c973c208cbdc71ae2279f30ef09f52472b9166")
    add_versions("3.8.6", "213269b05aac957c98f6e944774bb438d0bd168a2ec60b9e4f8d92035925821c")

    -- Replace the upstream deps list. `xz` is xz-utils / liblzma
    -- (provides lzma.h), the one libarchive's CMakeLists actually
    -- probes via find_package(LibLZMA). The other four match what
    -- libarchive expects already.
    add_deps("cmake")
    add_deps("zlib", "bzip2", "lz4", "zstd", "xz")

    if is_plat("windows") then
        add_syslinks("advapi32")
    end

    on_install("windows", "linux", "macosx", function (package)
        local configs = {
            -- Match xmake-repo's defaults...
            "-DENABLE_TEST=OFF",
            "-DENABLE_CAT=OFF",
            "-DENABLE_TAR=OFF",
            "-DENABLE_CPIO=OFF",
            "-DENABLE_OPENSSL=OFF",
            "-DENABLE_PCREPOSIX=OFF",
            "-DENABLE_LibGCC=OFF",
            "-DENABLE_CNG=OFF",
            "-DENABLE_ICONV=OFF",
            "-DENABLE_ACL=OFF",
            "-DENABLE_EXPAT=OFF",
            "-DENABLE_LIBXML2=OFF",
            "-DENABLE_LIBB2=OFF",
            -- ...and force-enable every compression backend we ship a
            -- dep for. cmake's auto-detection silently disables a
            -- backend if the find_package probe fails on the build
            -- host (e.g. minimal CI image with no system xz-dev),
            -- which is precisely what produced the original bug. Be
            -- explicit so the build either uses the xrepo-built
            -- backend or fails loudly.
            "-DENABLE_ZLIB=ON",
            "-DENABLE_BZip2=ON",
            "-DENABLE_LZ4=ON",
            "-DENABLE_ZSTD=ON",
            "-DENABLE_LZMA=ON",
        }
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
        table.insert(configs, "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"))
        if not package:config("shared") then
            package:add("defines", "LIBARCHIVE_STATIC")
        end
        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:has_cfuncs("archive_version_number", {includes = "archive.h"}))
    end)

package_end()
