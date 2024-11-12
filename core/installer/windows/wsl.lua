import("common")

function support()
    return {
        windows = true,
        linux = false,
        macosx = false
    }
end

function installed()
    return try {
        function()
            os.exec("wsl --status")
            return true
        end, catch {
            function(e)
                return false
            end
        }
    }
end

function install()
    print("[xlings]: Installing WSL...")

    return try {
        function()
            -- 先启用必要的 Windows 功能
            --enable_windows_features()

            -- 安装 WSL
            common.xlings_exec("winget install Microsoft.WSL --force")
            common.xlings_exec("wsl --install -d Ubuntu")

            cprint("\n\n  ${yellow}Note${clear}: Please restart your computer to complete WSL installation")
            cprint("  ${yellow}注意${clear}: 请重启你的电脑让WSL安装和配置生效\n\n")

            guide_wsl_setup()

            return true
        end, catch {
            function(e)
                print("[xlings]: Failed to install WSL: " .. e)
                cprint("${yellow}\n\t please use admin permission to run powershell or cmd, and retry install ${clear}\n")
                return false
            end
        }
    }
end

function uninstall()
    return try {
        function()
            common.xlings_exec("wsl --unregister Ubuntu")
            common.xlings_exec("winget uninstall Microsoft.WSL")
            return true
        end, catch {
            function(e)
                print("[xlings]: Failed to uninstall WSL: " .. e)
                return false
            end
        }
    }
end

function deps()
    return {
        windows = {},
        linux = {},
        macosx = {}
    }
end

function info()
    return {
        name = "wsl",
        homepage = "https://learn.microsoft.com/windows/wsl",
        author = "Microsoft",
        licenses = "MIT",
        github = "https://github.com/microsoft/WSL",
        docs = "https://learn.microsoft.com/windows/wsl",
        profile = "Windows Subsystem for Linux lets developers run a GNU/Linux environment directly on Windows",
    }
end

--- local functin

function enable_windows_features()
    print("[xlings]: Enabling required Windows features...")
    common.xlings_exec("dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart")
    common.xlings_exec("dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart")
end

function guide_wsl_setup()
    print([[

[xlings]: WSL Ubuntu 初步指导:

    0. 运行wsl: 通过 wsl -d ubuntu 命令 或点击命令行窗口顶部的下拉菜单 v 按钮选择ubuntu
    1. 等待 Ubuntu 初始化完成
    2. 创建一个新的 UNIX 用户名（不需要跟 Windows 用户名相同）
    3. 设置密码（输入时不会显示）
    4. 确认密码

注意:
- 首次安装需要重启电脑才能生效
- 用户名只能使用小写字母
- 密码输入时屏幕上不会显示任何内容
- 请记住您设置的密码，后续会用到

    ]])
end