import("platform")
import("common")

local config = platform.get_config_info()

local nvm_url = {
    linux = "https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.0/install.sh",
    windows = "https://github.com/coreybutler/nvm-windows/releases/download/1.1.11/nvm-setup.exe",
    -- macosx = "https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.0/install.sh"
}

local nvm_package = {
    linux = "nvm-install.sh",
    windows = "nvm-setup.exe",
}

local nvm_installer = path.join(config.rcachedir, nvm_package[os.host()])

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
            os.exec("nvm --version")
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing nvm...")

    local url = nvm_url[os.host()]

    if not os.isfile(nvm_installer) then
        common.xlings_download(url, nvm_installer)
    end

    return try {
        function ()
            if os.host() == "windows" then
                print("[xlings]: Running nvm installer, it may take some minutes...")
                common.xlings_exec(nvm_installer .. " /SILENT")

                local nvm_home = "C:\\Users\\" .. os.getenv("USERNAME") .. "\\AppData\\Roaming\\nvm"
                local node_home = "C:\\Program Files\\nodejs"

                os.setenv("NVM_HOME", nvm_home)
                os.setenv("NVM_SYMLINK", node_home)

                -- update path
                os.addenv("PATH", nvm_home)
                os.addenv("PATH", node_home)

            elseif os.host() == "linux" then
                os.exec("bash " .. nvm_installer)
                -- Source nvm in current shell
                os.exec("export NVM_DIR=\"$HOME/.nvm\"")
                os.exec("[ -s \"$NVM_DIR/nvm.sh\" ] && \\. \"$NVM_DIR/nvm.sh\"")
            elseif os.host() == "macosx" then
                -- TODO: install nvm on macosx
            end
            return true
        end, catch {
            function (e)
                os.tryrm(nvm_installer)
                return false
            end
        }
    }
end

function uninstall()
    if os.host() == "windows" then
        --common.xlings_exec("nvm uninstall")
    else
        os.tryrm("$HOME/.nvm")
    end
end

function deps()
    return {
        windows = {},
        linux = {
            "curl",
            "git"
        },
    }
end

function info()
    return {
        name = "nvm",
        homepage = "https://github.com/nvm-sh/nvm",
        author = "Tim Caswell",
        maintainers = "https://github.com/nvm-sh/nvm?tab=readme-ov-file#maintainers",
        licenses = "MIT",
        github = "https://github.com/nvm-sh/nvm",
        docs = "https://github.com/nvm-sh/nvm#installing-and-updating",
        profile = "Node Version Manager - Simple bash script to manage multiple active node.js versions",
    }
end