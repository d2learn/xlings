function _exec(cmd)
    local ok, stdout, stderr = os.execv(cmd[1], table.slice(cmd, 2))
    if not ok then
        return false, stderr
    end
    return true, stdout
end

function install(package_name)
    local cmd = {"sudo", "apt-get", "install", "-y", package_name}
    _exec(cmd)
end

function uninstall(package_name)
    local cmd = {"sudo", "apt-get", "remove", "-y", package_name}
    _exec(cmd)
end

function update()
    cmd = {"sudo", "apt-get", "update"}
    _exec(cmd)
end

function upgrade(package_name)
    local cmd = {"sudo", "apt-get", "upgrade", "-y", package_name}
    _exec(cmd)
end

function search(query)
    local cmd = {"apt-cache", "search", query}
    local ok, output = _exec(cmd)
    if not ok then
        return false
    end
    
    return true
end

function list_installed()
    local cmd = {"dpkg-query", "-W", "-f=${Package} ${Version} ${binary:Summary}\n"}
    local ok, output = _exec(cmd)
    if not ok then
        return false
    end
    return true
end

function info()
    return {
        name = "apt",
        homepage = "https://wiki.debian.org/Apt",
        author = "APT Development Team",
        licenses = "GPL-2.0",
        github = "https://salsa.debian.org/apt-team/apt",
        docs = "https://manpages.debian.org/apt",
        profile = "Advanced Package Tool (APT) is the default package manager for Debian-based Linux distributions",
    }
end