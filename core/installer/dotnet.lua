import("common")
import("platform")
import("installer.pkgmanager.xpkgmanager")

local config = platform.get_config_info()
local deb_url_template = "https://packages.microsoft.com/config/ubuntu/%s/packages-microsoft-prod.deb"
local deb_file = path.join(config.rcachedir, "packages-microsoft-prod.deb")

function support()
    return {
        windows = true,
        linux = true,
        macosx = false, -- TODO
    }
end

function installed()
    return try {
        function()
            os.exec("dotnet --version")
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing .NET SDK 8...")

    local host_info = common.get_linux_distribution()
    if host_info.name == "ubuntu" or host_info.name == "debian" then
        deb_url = string.format(deb_url_template, host_info.version)
        if not os.isfile(deb_file) and not use_winget_sys then
            common.xlings_download(deb_url, deb_file)
        end
    end

    return try {
        function ()
            if xpkgmanager.info().name == "apt" then
                os.exec("sudo dpkg -i " .. deb_file)
                -- TODO: optimize
                os.exec("sudo apt-get update")
                os.exec("sudo apt-get install -y apt-transport-https")
                os.exec("sudo apt-get update")
            end
            xpkgmanager.install("dotnet")
            print([[
[xlings]: dotnet tips:

    - dotnet --version
    - dotnet new console -o myApp
    - cd myApp
    - dotnet run
            ]])
            return true
        end, catch {
            function (e)
                print("[xlings]: Failed to install .NET SDK 8: " .. e)
                return false
            end
        }
    }
end

function info()
    return {
        name     = "dotnet-sdk-8",
        homepage = "https://dotnet.microsoft.com",
        author   = "Microsoft",
        licenses = "MIT",
        github   = "https://github.com/dotnet/sdk",
        docs     = "https://learn.microsoft.com/dotnet/",
        profile  = ".NET SDK 8 - Free. Cross-platform. Open source.",
    }
end