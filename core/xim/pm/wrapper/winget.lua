function support(name)
    local ok = os.execv("winget", {"show", name})
    return ok == 0
end

function installed(name)
    local output = os.iorunv("winget", {"list", name})
    if output then
        return output:lower():find(name:lower()) ~= nil
    else
        return false
    end
end

function deps(name)
    return nil, "winget does not provide dependency information"
end

function install(name)
    local ok = os.execv("winget", {"install", "-e", "--id", name})
    return ok == 0
end

function config(name)
    local ok = os.execv("winget", {"install", "-e", "--id", name, "--force"})
    return ok == 0
end

function uninstall(name)
    local ok = os.execv("winget", {"uninstall", "-e", "--id", name})
    return ok == 0
end

function info(name)
    local output = os.iorunv("winget", {"show", name})
    if output then
        return output:trim()
    else
        return nil, string.format("Failed to get information for package '%s'", name)
    end
end

function main()

end