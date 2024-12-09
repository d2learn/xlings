import("platform")
import("common")

local config = platform.get_config_info()

-- TODO: need mirror? https://mirrors.ustc.edu.cn/help/crates.io-index.html
local rust_url = {
    linux = "https://sh.rustup.rs",
    windows = "https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe",
    -- macosx = "xxx",
}

local vscode_package = {
    linux = "rustup-init.sh",
    windows = "rustup-init.exe",
}

local rust_installer = path.join(config.rcachedir, vscode_package[os.host()])

function support()
    return {
        windows = true,
        linux = true,
        macosx = false
    }
end

function installed()
    return try {
        function()
            os.exec("rustc --version")
            os.exec("cargo --version")
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing rust...")

    local url = rust_url[os.host()]

    if not os.isfile(rust_installer) then
        common.xlings_download(url, rust_installer)
    end

    function set_rust_env()
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
    end

    return try {
        function ()
            if os.host() == "windows" then
                -- select toolchain(abi) msvc or gnu
                local toolchain_abi = choice_toolchain()
                print("[xlings]: runninng rust installer, it may take some minutes...")
                common.xlings_exec(rust_installer
                    .. " --default-host " .. toolchain_abi
                    .. " --default-toolchain stable"
                    .. " --profile default -y"
                )
            elseif os.host() == "linux" then
                print("[xlings]: it may take some minutes...")
                os.exec("sh " .. rust_installer .. " -v -y")
            elseif os.host() == "macosx" then
                -- TODO: install rust on macosx
            end
            set_rust_env()
            return true
        end, catch {
            function (e)
                print("[xlings]: failed to install rust, error: %s", e)
                os.tryrm(rust_installer)
                return false
            end
        }
    }
end

function uninstall()
    common.xlings_exec("rustup self uninstall")
end

function deps()
    return {
        windows = {
            -- "vs"
        },
        linux = {

        },
    }
end

function info()
    return {
        name     = "rust",
        homepage = "https://www.rust-lang.org",
        author   = "rust team",
        maintainers = "https://prev.rust-lang.org/en-US/team.html",
        licenses = "MIT, Apache-2.0",
        github   = "https://github.com/rust-lang/rust",
        docs     = "https://prev.rust-lang.org/en-US/documentation.html",
        profile  = "A language empowering everyone to build reliable and efficient software.",
    }
end

-- local functions

-- host toolchain abi -- only for windows
function choice_toolchain()
    local toolchain_abi = "x86_64-pc-windows-gnu"
    print("[xlings]: Select toolchain ABI:")
    print([[

        1. x86_64-pc-windows-gnu (default)
        2. x86_64-pc-windows-msvc
    ]])
    cprint("${dim bright cyan}please input (1 or 2):${clear}")
    io.stdout:flush()
    local confirm = io.read()

    if confirm == "2" then
        toolchain_abi = "x86_64-pc-windows-msvc"
        local vs = import("xim.windows.visual_studio")
        if not vs.installed() then
            vs.install()
        end
    end

    return toolchain_abi
end

function main()
    local ct = choice_toolchain()
    print(ct)
end