package = {
    -- base info
    homepage = "https://www.rust-lang.org",

    name = "rust",
    version = "latest", -- default version
    description = "A language empowering everyone to build reliable and efficient software",

    authors = "rust team",
    maintainers = "https://prev.rust-lang.org/en-US/team.html",
    license = "MIT, Apache-2.0",
    repo = "https://github.com/rust-lang/rust",
    docs = "https://prev.rust-lang.org/en-US/documentation.html",

    -- xim pkg info
    status = "stable", -- dev, stable, deprecated
    categories = {"plang", "compiler"},
    keywords = {"Reliability", "Performance", "Productivity"},

    deps = {
        windows = {"visual-studio"},
    },

    pmanager = {
        ["latest"] = {
            windows = {
                xpm = {
                    url = "https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe",
                    sha256 = nil
                }
            },
            ubuntu = {
                xpm = { url = "https://sh.rustup.rs", sha256 = nil }
            },
        },
    }
}

function installed()
    os.exec("rustc --version")
    os.exec("cargo --version")
    return true
end

function install()
    if is_host("windows") then
        local toolchain_abi = _choice_toolchain()
        common.xlings_exec(
            "rustup-init.exe"
            .. " --default-host " .. toolchain_abi
            .. " --default-toolchain stable"
            .. " --profile default -y"
        )
    else
        os.exec("sh sh.rustup.rs -v -y")
    end
    return true
end

function config()
    if is_host("windows") then
        local home = os.getenv("USERPROFILE")
        local cargo_bin = path.join(home, ".cargo", "bin")
        local current_path = os.getenv("PATH") or ""

        os.setenv("CARGO_HOME", path.join(home, ".cargo"))
        os.setenv("RUSTUP_HOME", path.join(home, ".rustup"))

        if not current_path:find(cargo_bin, 1, true) then
            os.setenv("PATH", cargo_bin .. ";" .. current_path)
        end
    else
        -- Linux/macOS
        local home = os.getenv("HOME")
        os.addenv("PATH", path.join(home, ".cargo/bin"))
        --local cargo_env = path.join(home, ".cargo/env")
        --os.exec(". " .. cargo_env)
    end
    return true
end

function uninstall()
    os.exec("rustup self uninstall")
end

---------------------- private

-- host toolchain abi -- only for windows
function _choice_toolchain()
    local toolchain_abi = "x86_64-pc-windows-gnu"
    print("[xlings:xim]: Select toolchain ABI:")
    print([[

        1. x86_64-pc-windows-gnu (default)
        2. x86_64-pc-windows-msvc
    ]])
    cprint("${dim bright cyan}please input (1 or 2):${clear}")
    io.stdout:flush()
    local confirm = io.read()

    if confirm == "2" then
        toolchain_abi = "x86_64-pc-windows-msvc"
        local vs = import("installer.windows.visual_studio")
        if not vs.installed() then
            vs.install()
        end
    end

    return toolchain_abi
end