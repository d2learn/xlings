-- 检查包是否已安装（通过 `pacman -Q`）
function installed(name)
    return try { -- 此更改使得未安装此软件包时返回 nil 而不是终止程序
        function() return os.iorunv("pacman", { "-Q", name }) ~= nil end,
        catch { function() end }
    }
end

-- (从本地)获取包的依赖信息（通过 `expac -Q %D` 和备用办法 `pactree -d 1` 和终极备用办法...）
function deps(name)
    local output = nil

    if os.exists("/usr/bin/expac") then
        output = os.iorunv("expac", {"-Q", "%D", name})
    elseif os.exists("/usr/bin/pactree") then
        output = os.iorunv("pactree", {"-d", "1", name})
    else
        output = os.iorunv("bash", {"-c", "LANG=C.UTF-8 pacman -Qi " .. name .. " | awk '/Depends On/ {print $0}'"})
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
    local ok = os.execv("sudo", {"pacman", "-Sy", name})
    return ok == 0
end

-- 卸载包（通过 `pacman -R`）
function uninstall(name)
    local ok = os.execv("sudo", {"pacman", "-R", name})
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
    print(info("awa")) -- nil
end

