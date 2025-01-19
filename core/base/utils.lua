function url_to_domain(url)
    if not url or url == "" then
        print("Error: No URL provided!")
        return nil
    end

    -- 正则表达式匹配域名
    -- 匹配 http(s):// 或 git:// 后面的部分
    local pattern = "^[a-zA-Z]+://([^/]+)"
    local domain = url:match(pattern)

    -- 如果是 git@ 格式的 URL，例如 git@github.com:user/repo.git
    if not domain then
        pattern = "^git@([^:]+)" -- 匹配 git@ 后面的部分
        domain = url:match(pattern)
    end

    -- 如果匹配到了域名，进一步去除端口（如 :8080）
    if domain then
        domain = domain:match("([^:]+)") -- 去掉端口号（如 example.com:8080 -> example.com）
    end

    return domain
end

function url_latency(url)
    if not url or url == "" then
        print("Error: No URL provided!")
        return nil
    end

    url = url_to_domain(url)

    -- 设置空输出目标：Windows 使用 NUL，Linux 和 macOS 使用 /dev/null
    local null_output = os.host() == "windows" and "NUL" or "/dev/null"

    -- 定义 curl 命令，%{time_total} 用于提取总耗时
    local cmd = string.format("curl -o %s -s -w %%{time_total} %s", null_output, url)

    -- 执行 curl 命令并捕获结果
    local result, err = os.iorun(cmd)

    if not result or result == "" then
        print("Error: Failed to execute curl command! (" .. (err or "unknown error") .. ")")
        return nil
    end

    return tonumber(result) * 1000
end

function low_latency_urls(urls)
    if type(urls) ~= "table" then
        print("Error: URLs must be provided as a table!")
        return nil
    end

    local min_latency = math.huge
    local min_url = nil

    for _, url in ipairs(urls) do
        local latency = url_latency(url)
        cprint("${dim}Latency of URL '" .. url .. "': ${green}" .. latency .. "ms")
        if latency and latency < min_latency then
            min_latency = latency
            min_url = url
        end
    end

    return min_url
end

function main()
    -- test cases
    local urls = {
        "https://github.com",
        "https://gitee.com",
        "https://gitlab.com",
    }
    local fasturl = low_latency_urls(urls)
    cprint("${yellow}Fastest URL: " .. fasturl)
end
