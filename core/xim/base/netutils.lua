function url_to_domain(url)
    if not url or url == "" then
        print("Error: No URL provided!")
        return nil
    end

    local pattern = "^[a-zA-Z]+://([^/]+)"
    local domain = url:match(pattern)

    if not domain then
        pattern = "^git@([^:]+)"
        domain = url:match(pattern)
    end

    if domain then
        domain = domain:match("([^:]+)")
    end

    return domain
end

function url_latency(url)
    if not url or url == "" then
        print("Error: No URL provided!")
        return nil
    end

    url = url_to_domain(url)

    local null_output = os.host() == "windows" and "NUL" or "/dev/null"
    local cmd = string.format("curl -o %s -s -w %%{time_total} %s", null_output, url)

    local result, err = try {
        function ()
            return os.iorun(cmd)
        end, catch {
            function(e)
                return false, e
            end
        }
    }

    if not result or result == "" then
        print("Error: Failed to execute curl command! (" .. (err or "unknown error") .. ")")
        return 1000000 - 1
    end

    return tonumber(result) * 1000
end

function low_latency_urls(urls)
    if type(urls) ~= "table" then
        print("Error: URLs must be provided as a table!")
        return nil
    end

    local min_latency = 1000000
    local min_url = nil

    for _, url in ipairs(urls) do
        local latency = url_latency(url)
        cprint("${dim}[xlings]: Latency of URL '" .. url .. "': ${green}" .. latency .. "ms")
        if latency and latency < min_latency then
            min_latency = latency
            min_url = url
        end
    end

    return min_url
end

function load_module(fullpath, rootdir)
    local pattern = "^" .. rootdir:gsub("[%(%)%.%%%+%-%*%?%[%]%^%$]", "%%%1") .. "/?"
    local relative_path = fullpath:gsub(pattern, "")
    local path_parts = string.split(relative_path, "/")
    local module_path = table.concat(path_parts, "."):gsub("%.lua$", "")
    return inherit(module_path, {rootdir = rootdir})
end
