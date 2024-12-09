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
            if os.host() == "windows" then
                common.xlings_exec("nvm install lts")
                common.xlings_exec("nvm use lts")
                -- TODO: optimize
                cprint("\n\n  ${yellow}Note${clear}: please run ${cyan}nvm use lts${clear} to enable nodejs \n\n")
            elseif os.host() == "linux" or os.host() == "macosx" then
                -- TODO: fnm use lts-latest
                --common.xlings_exec("fnm install --lts")
                --common.xlings_exec("fnm use lts-latest")

                local node_version = "v22.11.0"
                common.xlings_exec("fnm install " .. node_version)
                common.xlings_exec("fnm use " .. node_version)

                local fnm_dir = os.getenv("FNM_DIR")
                local node_bin_dir = path.join(fnm_dir, "node-versions", node_version, "installation", "bin")
                print("node_bin_dir: " .. node_bin_dir)
                os.addenv("PATH", node_bin_dir)
            end

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
    -- TODO
    --common.xlings_exec("nvm uninstall lts")
end

function deps()
    return {
        windows = {
            "nvm"
        },
        linux = {
            "fnm"
        },
        macosx = {
            "fnm"
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