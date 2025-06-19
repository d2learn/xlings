-- 检查包是否已安装（通过 `pacman -Q`）
function installed(name)
    return os.iorunv("pacman", { "-Q", name }) ~= nil
end

-- (从本地)获取包的依赖信息（通过 `expac -Q %D` 和备用办法 `pactree -d 1` 和终极备用办法...）
function deps(name)
    local output = nil

    if os.exists("/usr/bin/expac") then
        output = os.iorunv("expac", {"-Q", "%D", name})
    elseif os.exists("/usr/bin/pactree") then
        output = os.iorunv("pactree", {"-d", "1", name})
    else
        -- 终极备用办法 使用英文环境执行 `pacman -Qi` 获取信息后 使用 `awk` 提取 `Depends On` 的那一行 ": " 后的部分
        output = os.iorunv("bash", {"-c", "LANG=C.UTF-8 pacman -Qi " .. name .. " | awk -F': ' '/Depends On/ {print $2}'"})
    end

    if output then
        return output:trim()
    else
        return nil, string.format("Failed to get dependencies for package '%s'", name)
    end
end

-- (更新数据库并)安装包（通过 `pacman -Sy`）
-- 但官方不推荐这样做 此处官方建议使用 `-Syu` 但这样也会进行系统更新 所以还是保留 `-Sy`
function install(name)
    local ok = os.execv("sudo", {"pacman", "--noconfirm", "-Sy", name})
    return ok == 0
end

-- 卸载包（通过 `pacman -R`）
function uninstall(name)
    local ok = os.execv("sudo", {"pacman", "--noconfirm", "-R", name})
    return ok == 0
end

-- (从服务器)获取包信息（通过 `pacman -Si`）
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
    print(type(info("awa"))) -- nil
end

