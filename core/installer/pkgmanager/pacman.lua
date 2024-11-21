function _exec(cmd)
    local ok, stdout, stderr = os.execv(cmd[1], table.slice(cmd, 2))
    if not ok then
        return false, stderr
    end
    return true, stdout
end

-- 安装包
function install(package_name)
    local cmd = {"sudo", "pacman", "-S", "--noconfirm", package_name}
    _exec(cmd)
end

function uninstall(package_name)
    local cmd = {"sudo", "pacman", "-R", "--noconfirm", package_name}
    _exec(cmd)
end

function update()
    local cmd = {"sudo", "pacman", "-Sy"}
    _exec(cmd)
end

function upgrade(package_name)
    local cmd = {"sudo", "pacman", "-S", "--noconfirm", package_name}
    _exec(cmd)
end

function search(query)
    local cmd = {"pacman", "-Ss", query}
    local ok, output = _exec(cmd)
    if not ok then
        return false
    end
    return true
end

function list_installed()
    local cmd = {"pacman", "-Q"}
    local ok, output = _exec(cmd)
    if not ok then
        return false
    end
    return true
end

function autoremove()
    
end

function show(package_name)

end

function info()
    return {
        name = "pacman",
        homepage = "https://archlinux.org/pacman",
        author = "Arch Linux Team",
        licenses = "GPL-2.0",
        github = "https://gitlab.archlinux.org/pacman/pacman",
        docs = "https://wiki.archlinux.org/title/Pacman",
        profile = "Pacman is the default package manager for Arch Linux",
    }
end