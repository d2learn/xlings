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
            -- common.xlings_exec("npm install -g npm@latest")
            cprint("\n\n  ${yellow}Note${clear}: please run ${cyan}nvm use lts${clear} to enable npm \n\n")
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