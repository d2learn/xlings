import("lib.detect.find_tool")

import("platform")
import("common")
import("installer.visual_studio")

local vstudio_url = "https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=Community&channel=Release&version=VS2022&source=VSLandingPage&passive=false"
local vstudio_setup = "VisualStudioSetup.exe"

local vstudio_setup_file = path.join(platform.get_config_info().rcachedir, vstudio_setup)
local vs_install_path = "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community"
local vs_cpp_components = "Microsoft.VisualStudio.Workload.NativeDesktop;Microsoft.VisualStudio.Component.VC.Tools.x86.x64"

function installed()
    if os.host() == "windows" then
        return visual_studio.installed()
    elseif os.host() == "linux" or os.host() == "macosx" then
        return nil ~= find_tool("g++", {version = true, force = true, paths = paths})
    else
        -- TODO
    end
end

function install()
    print("[xlings]: Installing c/cpp environment...")

    if os.host() == "windows" then
        visual_studio.install()
    elseif os.host() == "linux" then
        os.exec("sudo apt-get install g++ -y")
    elseif os.host() == "macosx" then
        -- TODO: install vscode on macosx
    end
end