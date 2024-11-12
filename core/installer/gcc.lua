import("lib.detect.find_tool")

import("platform")
import("common")
import("installer.windows.visual_studio")

function support()
    return {
        windows = false,
        linux = true,
        macosx = false
    }
end

function installed()
    return nil ~= find_tool("g++", {version = true, force = true, paths = paths})
end

function install()
    print("[xlings]: Installing gcc environment...")

    if os.host() == "windows" then
        -- TODO: ...
    elseif os.host() == "linux" then
        os.exec("sudo apt-get install g++ -y")
    elseif os.host() == "macosx" then
        -- TODO: install vscode on macosx
    end

    return true
end