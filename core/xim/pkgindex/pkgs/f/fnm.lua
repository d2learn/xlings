import("common")

local nvm_url = "https://fnm.vercel.app/install"
local fnm_installer = "/tmp/fnm_installer.sh"

function support()
    return {
        windows = true,
        linux = true,
        macosx = true
    }
end

function installed()
    return try {
        function()
            os.exec("fnm --version")
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing fnm...")

    return try {
        function ()
            if os.host() == "linux" or os.host() == "macosx" then
                if not os.isfile(fnm_installer) then
                    common.xlings_download(nvm_url, fnm_installer)
                end
                -- Execute the install script directly using curl
                os.exec("bash " .. fnm_installer)
                local fnm_path = os.getenv("HOME") .. "/.local/share/fnm"
                os.setenv("FNM_PATH", fnm_path)
                os.addenv("PATH", fnm_path)
                local output, err = os.iorun("fnm env")
                local fnm_config = parse_exports(output)
                for key, value in pairs(fnm_config) do
                    os.setenv(key, value)
                end
                --print(fnm_config)
                os.addenv("PATH", fnm_config.FNM_MULTISHELL_PATH)
            else
                os.exec("winget install Schniz.fnm")
            end
            return true
        end, catch {
            function (e)
                return false
            end
        }
    }
end

function uninstall()
    -- TODO: Implement uninstallation
end

function deps()
    return {
        windows = {},
        linux = {
            --"curl",
            --"unzip"
        },
        macosx = {
            --"curl",
            --"unzip"
        }
    }
end

function info()
    return {
        name = "fnm",
        homepage = "https://github.com/Schniz/fnm",
        author = "Gal Schlezinger",
        licenses = "GPL-3.0",
        github = "https://github.com/Schniz/fnm",
        docs = "https://github.com/Schniz/fnm#installation",
        profile = "🚀 Fast and simple Node.js version manager, built in Rust",
    }
end

--- local function

function parse_exports(exports_str)

    -- Initialize config with empty strings
    local fnm_config = {
        FNM_MULTISHELL_PATH = "",
        FNM_VERSION_FILE_STRATEGY = "",
        FNM_DIR = "",
        FNM_LOGLEVEL = "",
        FNM_NODE_DIST_MIRROR = "",
        FNM_COREPACK_ENABLED = "",
        FNM_RESOLVE_ENGINES = "",
        FNM_ARCH = ""
    }

    for line in exports_str:gmatch("[^\r\n]+") do
        local var, value = line:match('export ([^=]+)="([^"]+)"')
        if var and fnm_config[var] ~= nil then
            fnm_config[var] = value
        end
    end
    return fnm_config
end

function main()
    install()
end