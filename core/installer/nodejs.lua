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
            os.exec("node --version")
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing Node.js...")

    return try {
        function()
            common.xlings_exec("nvm install lts")
            common.xlings_exec("nvm use lts")
            -- TODO: optimize
            cprint("\n\n  ${yellow}Note${clear}: please run ${cyan}nvm use lts${clear} to enable nodejs \n\n")
            return true
        end, catch {
            function(e)
                print("[xlings]: Failed to install Node.js: " .. e)
                return false
            end
        }
    }
end

function uninstall()
    common.xlings_exec("nvm uninstall lts")
end

function deps()
    return {
        windows = {
            "nvm"
        },
        linux = {
            "nvm"
        },
        macosx = {
            "nvm"
        }
    }
end

function info()
    return {
        name = "nodejs",
        homepage = "https://nodejs.org",
        author = "Node.js Foundation",
        licenses = "MIT",
        github = "https://github.com/nodejs/node",
        docs = "https://nodejs.org/docs",
        profile = "Node.js is a JavaScript runtime built on Chrome's V8 JavaScript engine",
    }
end