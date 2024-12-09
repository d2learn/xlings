import("lib.detect.find_tool")

import("platform")
import("common")

local config = platform.get_config_info()

local devcpp_url = "https://gitee.com/sunrisepeak/xlings-pkg/releases/download/devcpp/devcpp.exe"
local devcpp_installer = path.join(config.rcachedir, "devcpp-installer.exe")

function support()
    return {
        windows = true,
        linux = false,
        macosx = false
    }
end

function installed()
    if os.isfile("C:\\Program Files (x86)\\Dev-Cpp\\devcpp.exe") then
        return true
    else
        return false
    end
end

function install()
    print("[xlings]: Installing Dev-C++...")

    if not os.isfile(devcpp_installer) then
        common.xlings_download(devcpp_url, devcpp_installer)
    end

    print("[xlings]: suggestion use default install path for dev-c++")
    print("Dev-C++安装建议:")
    print("\t 0.打开安装提示")
    print("\t 1.先选English")
    print("\t 2.使用默认选项安装")
    print("\t 3.打开Dev-C++(这里可以重新选择IDE语言)")

    return try {
        function ()
            common.xlings_exec(devcpp_installer)
            return true
        end, catch {
            function (e)
                os.tryrm(devcpp_installer)
                return false
            end
        }
    }
end