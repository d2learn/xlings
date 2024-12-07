-- 解析单个版本字符串到版本对象
function parse_single_version(version_str)
    local version = {}
    if version_str == "any" or version_str == "*" then
        return version end
    -- 处理带比较符号的版本号
    local op, ver = string.match(version_str, "([<>=~]+)(.+)")
    if op and ver then if false then
        elseif op:find(">=") then
            version.min = ver
            version.min_inclusive = true
        elseif op:find("<=") then
            version.max = ver
            version.max_inclusive = true
        elseif op:find(">")  then
            version.min = ver
            version.min_inclusive = false
        elseif op:find("<")  then
            version.max = ver
            version.max_inclusive = false
        elseif op:find("~")  then
            local major, minor, patch = ver:match("^([0-9%.]*)(%d+)%.(%d+)$")
            if major and minor and patch then
                version.min = ver
                version.min_inclusive = true
                version.max = major .. (tonumber(minor) + 1) .. ".0"
                version.max_inclusive = false
            else
                print("Invalid version format for ~ operator: " .. version_str)
            end
        end
    elseif version_str:match("%d+%.%d+%.x") then
        -- 处理 .x 的情况
        local major, minor = version_str:match("^(%d+)%.(%d+)%.")
        if major and minor then
            version.min = major .. "." .. minor .. ".0"
            version.min_inclusive = true
            version.max = major .. "." .. (tonumber(minor) + 1) .. ".0"
            version.max_inclusive = false
        else
            print("Invalid version format for .x: " .. version_str)
        end
    else
        -- 如果没有比较符，则认为是精确匹配
        version.min = version_str
        version.min_inclusive = true
        version.max = version_str
        version.max_inclusive = true
    end
    return version
end

-- 解析复合版本字符串到版本对象
function parse_version(version_str)
    local conditions = {}
    for cond in version_str:gmatch("[^ ,]+") do
        table.insert(conditions, parse_single_version(cond))
    end
    local merged_version
    for _, cond in ipairs(conditions) do
        merged_version = merge_range(merged_version, cond)
    end
    return merged_version or { min = nil, max = nil }
end

-- 合并两个版本范围
function merge_range(a, b)
    if not a then return b end
    if not b then return a end
    local merged = {}
    -- 确定最小版本
    if not a.min or (b.min and compare_version(b.min, a.min) > 0) then
        merged.min = b.min
        merged.min_inclusive = b.min_inclusive
    else
        merged.min = a.min
        merged.min_inclusive = a.min_inclusive
    end
    -- 确定最大版本
    if not a.max or (b.max and compare_version(b.max, a.max) < 0) then
        merged.max = b.max
        merged.max_inclusive = b.max_inclusive
    else
        merged.max = a.max
        merged.max_inclusive = a.max_inclusive
    end
    return merged
end

-- 比较两个版本字符串

function compare_version(a, b)
    -- TODO 这正则没研究明白
    local av, ar = a:match("^([%d%.]+)([-.+]?.*)$")
    local bv, br = b:match("^([%d%.]+)([-.+]?.*)$")
    av = av or a; bv = bv or b
    local cv = compare_version_number(av, bv)
    if cv ~= 0 then return cv end
    ar = string.sub(ar, -#ar+1)
    br = string.sub(br, -#br+1)
    return compare_version_suffix(ar, br)
end

function compare_version_number(a, b)
    --print("comparing number", a or "nil", b or "nil")
    if not a then return -1 end
    if not b then return 1 end
    if a == b then return 0 end
    local a_parts = {}
    for num in a:gmatch("%d+") do table.insert(a_parts, tonumber(num)) end
    local b_parts = {}
    for num in b:gmatch("%d+") do table.insert(b_parts, tonumber(num)) end
    local length = math.max(#a_parts, #b_parts)
    while #a_parts < length do table.insert(a_parts, 0) end
    while #b_parts < length do table.insert(b_parts, 0) end
    for i = 1, length do if a_parts[i] ~= b_parts[i] then
        return a_parts[i] > b_parts[i] and 1 or -1
    end end
    return 0
end

pre_flags = {
    rc = -1, r = -1, pre = -1,
    beta = -2, b = -2,
    alpha = -3, a = -3,
}
function compare_version_suffix(a, b)
    --print("comparing suffix", a or "nil", b or "nil")
    a = a or ""; b = b or ""
    if a == b then return 0 end
    if a == "" and b ~= "" then return  1 end
    if b == "" and a ~= "" then return -1 end

    local as, av = a:match("^(%a+)(%d*)$")
    local bs, bv = b:match("^(%a+)(%d*)$")
    as = pre_flags[as]
    bs = pre_flags[bs]
    if as ~= bs then return as > bs and 1 or -1 end
    if av ~= bv then return av > bv and 1 or -1 end

    return 0
end

-- 生成所需依赖
function gen_deps(deps)
    local needed_deps = {}
    for _, dep_set in ipairs(deps) do for pkg_name, version_str in pairs(dep_set) do
        needed_deps[pkg_name] = not needed_deps[pkg_name] and
                parse_version(version_str) or
                merge_range(needed_deps[pkg_name], parse_version(version_str))
    end end return needed_deps
end


-- 测试代码
function main()
    local deps = {
        { packageA = "*", packageB = "*", packageC = ">=2" },
        { packageB = "~5.7.1", packageC = "~3.2.7" },
        { packageC = "3.0.x" },
        { packageC = "3.0.5", packageD = ">=3 <4" },
        { packageE = ">3.15" },
    }
    local needed_deps = gen_deps(deps)
    for pkg, range in pairs(needed_deps) do
        print(pkg, range)
    end

    local versions = {
        { a = "1.2.3", b = "1.3.2" },
        { a = "1.0.0-beta1", b = "1.0.0-beta2" },
        { a = "1.0.0", b = "1.0-beta3" },
        { a = "1.0.0-alpha1", b = "1.0.0-beta2" }
    }
    local function c(a, b)
        local r = compare_version(a, b)
        if r == 0 then return "[ (==) equals (==) ]" end
        return r > 0 and "[ greater (>) than ]" or "[ less (<) than ]"
    end
    for _, v in ipairs(versions) do
        print(v.a, c(v.a, v.b), v.b)
    end
end