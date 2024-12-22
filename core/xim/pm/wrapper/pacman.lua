-- 检查包是否已安装（通过 `pacman -Q`）
function installed(name)
    return os.iorunv("pacman", {"-Q", name}) ~= nil
end

-- 获取包的依赖信息（通过 `pactree -d 1`）
function deps(name)
    local output = os.iorunv("pactree", {"-d", "1", name})
    if output then
        return output:trim()
    else
        return nil, string.format("Failed to get dependencies for package '%s'", name)
    end
end

-- 安装包（通过 `pacman -S`）
function install(name)
    local ok = os.execv("sudo", {"pacman", "-S", name})
    return ok == 0
end

-- 卸载包（通过 `pacman -R`）
function uninstall(name)
    local ok = os.execv("sudo", {"pacman", "-R", name})
    return ok == 0
end

-- 获取包信息（通过 `pacman -Si`）
function info(name)
    local output = os.iorunv("pacman", {"-Si", name})
    if output then
        return output:trim()
    else
        return nil, string.format("Failed to get information for package '%s'", name)
    end
end

function main()
    print(installed("linux")) -- true
    print(deps("code"))
    -- print(install("code"))
    print(info("code"))
    print(info("awa")) -- error: 错误：软件包 'awa' 未找到
end

