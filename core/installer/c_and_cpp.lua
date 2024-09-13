import("platform")
import("common")

function installed()
    local detect_cmd = "g++ --version"
    if os.host() == "windows" then
        detect_cmd = "cl --version"
    elseif os.host() == "linux" or os.host() == "macosx" then
    	detect_cmd = "g++ --version"
    else
        -- TODO
    end

    return try {
        function()
            os.exec(detect_cmd)
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing c/cpp environment...")

    if os.host() == "windows" then
        -- TODO: install vscode on windows
    elseif os.host() == "linux" then
        os.exec("sudo apt-get install g++ -y")
    elseif os.host() == "macosx" then
        -- TODO: install vscode on macosx
    end
end