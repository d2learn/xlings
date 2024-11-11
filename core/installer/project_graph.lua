import("lib.detect.find_tool")

import("platform")
import("common")
import("installer.base.github")

local config = platform.get_config_info()

local installer_match = {
    linux = "deb",
    windows = "exe",
}

function support()
    return {
        windows = true,
        linux = true,
        --macosx = true
    }
end

function installed()
    local tool = find_tool("project-graph")
    if tool then return true end
    return false
end

function install()
    print("[xlings]: Installing Project Graph...")

    local installer_info = github.get_latest_release_url(
        "LiRenTech", "project-graph",
        installer_match[os.host()]
    )

    local pgraph_installer = nil

    return try {
        function()
            print(installer_info)
            pgraph_installer = path.join(config.rcachedir, installer_info.name)
            if not os.isfile(pgraph_installer) then
                common.xlings_download(installer_info.url, pgraph_installer)
            end
            common.xlings_exec(pgraph_installer .. " /SILENT")
            return true
        end, catch {
            function(e)
                print("[xlings]: Failed to install Project Graph: " .. e)
                if pgraph_installer then os.tryrm(pgraph_installer) end
                return false
            end
        }
    }
end

function uninstall()
    -- TODO
end

function deps()
    return {}
end

function info()
    return {
        name = "Project Graph",
        homepage = "https://liren.zty012.de/project-graph",
        author = "LiRenTech Team",
        licenses = "MIT",
        github = "https://github.com/LiRenTech/project-graph",
        profile = "快速绘制节点图的桌面工具，可以用于项目进程拓扑图绘制、快速头脑风暴草稿",
    }
end