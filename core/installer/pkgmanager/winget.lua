function _exec(cmd)
    local ok, stdout, stderr = os.execv(cmd[1], table.slice(cmd, 2))
    if not ok then
        return false, stderr
    end
    return true, stdout
end

function install(package_name)
    local cmd = {"winget", "install", "--silent", package_name}
    _exec(cmd)
end

function uninstall(package_name)
    local cmd = {"winget", "uninstall", "--silent", package_name}
    _exec(cmd)
end

function update()
    local cmd = {"winget", "source", "update"}
    _exec(cmd)
end

function upgrade(package_name)
    local cmd = {"winget", "upgrade", "--silent", package_name}
    _exec(cmd)
end

function search(query)
    local cmd = {"winget", "search", query, "--accept-source-agreements"}
    _exec(cmd)
end

function list_installed()
    local cmd = {"winget", "list"}
    _exec(cmd)
end

function info()
    return {
        name = "winget",
        homepage = "https://learn.microsoft.com/windows/package-manager/",
        author = "Microsoft Corporation",
        licenses = "MIT",
        github = "https://github.com/microsoft/winget-cli",
        docs = "https://learn.microsoft.com/windows/package-manager/winget/",
        profile = "Windows Package Manager CLI (winget) is the package manager for Windows",
    }
end