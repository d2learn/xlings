import("platform")
import("common")

function installed()
    return try {
        function()
            if os.host() == "windows" then
                os.exec("python --version")
            else
                os.exec("python3 --version")
            end
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing cpp environment...")

    if os.host() == "windows" then
        -- TODO: install vscode on windows
    elseif os.host() == "linux" then
        os.exec("sudo apt-get install python3 -y")
    elseif os.host() == "macosx" then
        -- TODO: install vscode on macosx
    end
end