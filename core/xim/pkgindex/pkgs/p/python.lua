import("platform")
import("common")

-- local python_url = "https://www.python.org/ftp/python/3.12.6/python-3.12.6-amd64.exe"
local python_url = "https://gitee.com/sunrisepeak/xlings-pkg/releases/download/python12/python-3.12.6-amd64.exe"
local python_installer_file = path.join(platform.get_config_info().rcachedir, "python-installer.exe")

function support()
    return {
        windows = true,
        linux = true,
        macosx = false
    }
end

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
    print("[xlings]: Installing python environment...")

    if os.host() == "windows" then
        -- TODO: install vscode on windows
        if not os.isfile(python_installer_file) then
            common.xlings_download(python_url, python_installer_file)
        end

        try {
            function ()
                -- use xlings_adnim request admin permissions(only for windows)
                common.xlings_run_bat_script(
                    python_installer_file .. [[ /passive InstallAllUsers=1 PrependPath=1 Include_test=1 ]],
                    true
                )
            end, catch {
                function (e)
                    print(e)
                    os.tryrm(python_installer_file)
                end
            }
        }
    elseif os.host() == "linux" then
        os.exec("sudo apt-get install python3 -y")
    elseif os.host() == "macosx" then
        -- TODO: install vscode on macosx
    end
    return true
end