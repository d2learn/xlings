import("platform")
import("common")

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
            common.xlings_exec("npm --version")
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing npm...")

    return try {
        function()
            local cmd_str = ""
            -- common.xlings_exec("npm install -g npm@latest")
            if os.host() == "windows" then
                cmd_str = "nvm use lts"
            elseif os.host() == "linux" or os.host() == "macosx" then
                -- TOOD: fnm use lts-latest
                cmd_str = "fnm use lts-latest"
            end

            cprint("\n\n  ${yellow}Note${clear}: please run ${cyan}" .. cmd_str .. "${clear} to enable npm \n\n")

            os.exec("npm --version")

            return true
        end, catch {
            function(e)
                print("[xlings]: Failed to install npm: " .. e)
                return false
            end
        }
    }
end

function uninstall()
    -- TODO
end

function deps()
    return {
        windows = {
            "nodejs"
        },
        linux = {
            "nodejs"
        },
        macosx = {
            "nodejs"
        }
    }
end

function info()
    return {
        name = "npm",
        homepage = "https://www.npmjs.com",
        author = "npm, Inc.",
        licenses = "Artistic-2.0",
        github = "https://github.com/npm/cli",
        docs = "https://docs.npmjs.com",
        profile = "npm is the package manager for the Node.js JavaScript platform",
    }
end