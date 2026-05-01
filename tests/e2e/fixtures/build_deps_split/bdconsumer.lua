-- Fixture: bdconsumer — exercises the build/runtime deps split. Its
-- xpm.linux.deps uses the new table form { runtime = ..., build = ... }
-- so the loader exercises the split-form path and the resolver
-- exercises kind-aware DepKind propagation.
--
-- The install hook captures whether xlings injected the build-dep env
-- vars and whether the build dep was reachable on PATH. The e2e test
-- (tests/e2e/build_deps_split_test.sh) reads these capture files to
-- verify both behaviours.
package = {
    spec = "1",
    name = "bdconsumer",
    description = "Fixture: package with both runtime and build deps",
    licenses = {"MIT"},
    type = "package",
    repo = "https://example.com/bdconsumer",
    archs = {"x86_64"},
    xvm_enable = true,

    xpm = {
        linux = {
            deps = {
                runtime = { "rttool" },
                build   = { "bdtool" },
            },
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"]  = {},
        },
        macosx = {
            deps = {
                runtime = { "rttool" },
                build   = { "bdtool" },
            },
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"]  = {},
        },
        windows = {
            deps = {
                runtime = { "rttool" },
                build   = { "bdtool" },
            },
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"]  = {},
        },
    },
}

import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")

function install()
    local dir = pkginfo.install_dir()
    os.mkdir(dir)

    -- 1. Capture XLINGS_BUILDDEP_*_PATH env vars: the installer should
    --    set BDTOOL for the build dep but NOT RTTOOL (runtime deps are
    --    activated via the workspace, not the env-var pathway).
    local bdpath = os.getenv("XLINGS_BUILDDEP_BDTOOL_PATH") or ""
    local rt_env = os.getenv("XLINGS_BUILDDEP_RTTOOL_PATH") or ""
    local f = io.open(path.join(dir, "captured_env.txt"), "w")
    f:write("BDTOOL_PATH=" .. bdpath .. "\n")
    f:write("RTTOOL_ENV=" .. rt_env .. "\n")
    f:close()

    -- 2. Try to invoke `bdtool` via PATH. The installer should have
    --    prepended the build dep's bin/ directory to PATH for the
    --    duration of this hook, so the bare-name call must resolve.
    local h = io.popen("bdtool 2>&1")
    if h then
        local out = h:read("*a") or ""
        h:close()
        local f2 = io.open(path.join(dir, "bdtool_output.txt"), "w")
        if f2 then f2:write(out); f2:close() end
    end

    -- 3. Use pkginfo.build_dep() — the high-level Lua API for build deps
    local info = pkginfo.build_dep("bdtool")
    local f3 = io.open(path.join(dir, "build_dep_api.txt"), "w")
    if info then
        f3:write("path=" .. (info.path or "") .. "\n")
        f3:write("bin="  .. (info.bin  or "") .. "\n")
        f3:write("version=" .. (info.version or "") .. "\n")
    else
        f3:write("nil\n")
    end
    f3:close()

    return true
end

function config()
    xvm.add("bdconsumer", { bindir = pkginfo.install_dir() })
    return true
end

function uninstall()
    xvm.remove("bdconsumer")
    return true
end
