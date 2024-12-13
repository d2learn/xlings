package = {
    homepage = "https://code.visualstudio.com",

    name = "vscode",
    description = "Visual Studio Code",
    contributors = "https://github.com/microsoft/vscode/graphs/contributors",
    license = "MIT",
    repo = "https://github.com/microsoft/vscode",
    docs = "https://code.visualstudio.com/docs",

    status = "stable",
    categories = { "editor", "tools" },
    keywords = { "vscode", "cross-platform" },
    date = "2024-9-01",

    xpm = {
        ubuntu = {
            ["latest"] = { ref = "1.93.1" },
            ["1.93.1"] = {
                url = "https://vscode.download.prss.microsoft.com/dbazure/download/stable/38c31bc77e0dd6ae88a4e9cc93428cc27a56ba40/code_1.93.1-1726079302_amd64.deb",
                sha256 = "29a9431daea5307cf9a22f6a95cbbe328f48ace6bda126457e1171390dc84aed"
            }
        },
    },

    pm_wrapper = {
        winget = "Microsoft.VisualStudioCode",
        pacman = "code",
    }
}

import("xim.base.runtime")

function installed()
    os.exec("code --version")
    return true
end

function install()
    local pkginfo = runtime.get_pkginfo()

    os.exec("sudo dpkg -i " .. pkginfo.install_file)

    -- tips
    print([[
[xlings]: vscode Tips/小提示:
- auto save settings: File -> Auto Save
- 自动保存设置: 文件 -> 自动保存
    ]])

    return true
end

function uninstall()
    os.exec("sudo dpkg --remove code")
end
