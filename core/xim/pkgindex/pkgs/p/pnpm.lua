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
            --os.exec("pnpm --version")
            common.xlings_exec("pnpm --version")
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing pnpm...")

    return try {
        function()
            common.xlings_exec("npm install -g pnpm")
            return true
        end, catch {
            function(e)
                print("[xlings]: Failed to install pnpm: " .. e)
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
            "npm"
        },
        linux = {
            "npm"
        },
        macosx = {
            "npm" 
        }
    }
end

function info()
    return {
        name = "pnpm",
        homepage = "https://pnpm.io",
        author = "Zoltan Kochan",
        licenses = "MIT",
        github = "https://github.com/pnpm/pnpm",
        docs = "https://pnpm.io/motivation",
        profile = "pnpm is a fast, disk space efficient package manager for Node.js",
    }
end