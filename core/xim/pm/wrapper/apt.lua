-- 检查包是否已安装（通过 `apt list --installed`）
function installed(name)
    local output = os.iorunv("apt", {"list", "--installed", name})
    return output and output:find("%[installed%]") ~= nil
end

-- 获取包的依赖信息（通过 `apt-cache depends`）
function deps(name)
    local output = os.iorunv("apt-cache", {"depends", name})
    if output then
        return output:trim()
    else
        return nil, string.format("Failed to get dependencies for package '%s'", name)
    end
end

-- 安装包（通过 `apt install`）
function install(name)
    local ok = os.execv("sudo", {"apt", "-y", "install", name})
    return ok == 0
end

-- 卸载包（通过 `apt remove`）
function uninstall(name)
    local ok = os.execv("sudo", {"apt", "-y", "remove", name})
    return ok == 0
end

-- 获取包信息（通过 `apt show`）
function info(name)
    local output = os.iorunv("apt", {"show", name})
    if output then
        return output:trim()
    else
        return nil, string.format("Failed to get information for package '%s'", name)
    end
end

function main()
    print(info("vim"))
end

