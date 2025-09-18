import("net.http")
import("utils.archive")
import("platform")
import("devel.git")
import("lib.detect.find_tool")
import("async.runjobs")

--local common = {}

function xlings_str_format(s)
    return tostring(s):gsub("\\", "\\\\")
            :gsub("\n", "\\n")
            :gsub("\"", "\\\"")
            :gsub("\t", "\\t")
            :gsub("\r", "\\r")
            -- remove ansi color
            -- https://github.com/fluent/fluent-bit/discussions/6151
            -- strip ansi https://stackoverflow.com/a/49209650/368691
            :gsub('[\27\155][][()#;?%d]*[A-PRZcf-ntqry=><~]', '')
end

function xlings_str_split(str, delimiter)
    local result = {}
    local from = 1
    local delim_from, delim_to = string.find(str, delimiter, from)
    while delim_from do
        table.insert(result, string.sub(str, from, delim_from-1))
        from = delim_to + 1
        delim_from, delim_to = string.find(str, delimiter, from)
    end
    table.insert(result, string.sub(str, from))
    return result
end

function xlings_str_trim(s)
    return s:match("^%s*(.-)%s*$")
end

-- lua style (only support var)
function xlings_config_file_parse(filename)
    local config = {}
    local file, err
    try
    {
        function()
            file = io.open(filename, "r")
        end,
        catch
        {
            function (e)
                err = e
            end
        }
    }
    
    if not file then
        cprint("${yellow}[xlings]: " .. err)
        os.sleep(1000)
        return config
    end

    local multi_line_value = false
    local key, value
    for line in file:lines() do
        -- not match
        if multi_line_value then
            if line:find("]]") then
                config[key] = config[key] .. "\n" .. line:sub(1, -3)
                multi_line_value = false
            else
                config[key] = config[key] .. "\n" .. line
            end
        else
            -- key only match a_b_id -> id
            key, value = line:match("(%w+)%s*=%s*(.+)")
            if key and value then
                --value = value:trim()
                value = value:match("^%s*(.-)%s*$")
                if value:sub(1, 1) == "\"" and value:sub(-1) == "\"" then
                    value = value:sub(2, -2)
                elseif value:sub(1, 2) == "[[" then
                    value = value:sub(3)
                    multi_line_value = true
                end
                config[key] = value
            end
        end
    end
    file:close()
    return config
end

function xlings_save_config_to_file(config, file)
    local file, err = io.open(file, "w")

    if not file then
        print("[xlings]: Error opening file: " .. err)
        return
    end

    for k, v in pairs(config) do
        file:write(k .. " = \"" .. v .. "\"\n")
    end
    file:close()
end

function xlings_exec(cmd)
    os.exec(platform.get_config_info().cmd_wrapper .. tostring(cmd))
end

function xlings_run_bat_script(content, use_adnim) -- only for windows
    local script_file = path.join(platform.get_config_info().rcachedir, "win_tmp.bat")
    xlings_create_file_and_write(script_file, content)
    if use_adnim then
        os.exec([[powershell -Command "Start-Process ']] .. script_file .. [[' -Verb RunAs -Wait]])
    else
        os.exec(script_file)
    end
end

function xlings_python(file)
    if os.host() == "windows" then
        file = file:gsub("/", "\\")
        xlings_exec("python " .. file)
    else
        xlings_exec("python3 " .. file)
    end
end

function xlings_clear_screen()
    --[[
        if os.host() == "windows" then
            os.exec("xligns_clear.bat")
        else
            os.exec("clear")
        end
    ]]
    os.exec(platform.get_config_info().cmd_clear)
end

function xlings_download(url, dest)
    cprint("[xlings]: downloading: ${dim}%s to %s", url, dest)

    return try {
        function()
            local tool = find_tool("curl")
            if tool then -- support progress-bar
                local outputdir = path.directory(dest)
                if not os.isdir(outputdir) then
                    os.mkdir(outputdir)
                end
                -- -#, --progress-bar
                -- -L, --location):
                xlings_exec("\"" .. tool.program .. "\"" .. " -L -# -o " .. dest .. " " .. url)
                --os.vrunv(tool.program, {"-#", "-o", dest, url})
            else
                -- { insecure = true }
                runjobs("download",function () http.download(url, dest) end, { progress = true })
            end
            return true
        end,
        catch {
            function (e)
                print(e)
                cprint("\n\t${yellow}Please check your network environment${clear}\n")
                return false
            end
        }
    }


end

function xlings_create_file_and_write(file, content)
    local file, err = io.open(file, "w")

    if not file then
        print("[xlings]: Error opening file: " .. err)
        return
    end

    file:write(content)
    file:close()
end

function xlings_file_append(file, content)
    local file, err = io.open(file, "a")

    if not file then
        print("[xlings]: Error opening file: " .. err)
        return
    end

    file:write(content)
    file:close()
end

function xlings_read_file(file)
    local file, err = io.open(file, "r")

    if not file then
        print("[xlings]: Error opening file: " .. err)
        return
    end

    local content = file:read("*a")
    file:close()
    return content
end

function xlings_path_format(path)
    path = tostring(path)

    path = path:gsub("\\", "/")
    
    -- remove ./
    path = path:gsub("%./", "")
    
    -- remove ../
    --path = path:gsub("%.%.%/", "")
    
    -- /// -> /
    path = path:gsub("/+", "/")

    -- remove leading and trailing whitespaces
    path = string.gsub(path, "^%s*(.-)%s*$", "%1")

    return path
end

-- download repo from git_url to destination directory
function xlings_download_repo(git_url, dest_dir)
    local repo_name = git_url:match("([^/]+)%.git$")
    dest_dir = dest_dir .. "/" .. repo_name
    cprint("[xlings]: downloading ${magenta}%s${clear} from %s", repo_name, git_url)
    if os.isdir(dest_dir) then
        cprint("[xlings]: " .. repo_name .. " already exists.")
        return
    end
    git.clone(
        git_url,
        {
            depth = 1, branch = "main",
            outputdir = dest_dir, recursive = true,
        }
    )
    cprint("[xlings]: %s - ${green}ok${clear}", dest_dir)
end


function get_linux_distribution()
    local os_release = io.open("/etc/os-release", "r")
    if os_release then
        local content = os_release:read("*a")
        os_release:close()
        
        local id = content:match('ID=["]*([^"\n]+)["]*')
        local version = content:match('VERSION_ID=["]*([^"\n]+)["]*')
        local name = content:match('PRETTY_NAME=["]*([^"\n]+)["]*')

        -- name use lowercase
        if name then
            name = name:lower()
            name = name:gsub("%s+%d[%d%.]*%s*.*$", "")
        end
        
        return {
            id = id or "unknown",
            version = version or "unknown",
            name = name or "unknown"
        }
    end

    return {
        id = "unknown",
        version = "unknown",
        name = "unknown"
    }
end


function xlings_input_process(action, args, kv_cmds)

    local main_target = ""

    action = tostring(action)
    args = args or {}

    kv_cmds = kv_cmds or {
        ["--default"] = false,  -- -list (string)
    }

    if #args > 0 and args[1]:sub(1, 1) ~= '-' then
        main_target = args[1]
    end

    for i = 1, #args do
        if kv_cmds[action .. args[i]] == false then
            kv_cmds[action .. args[i]] = args[i + 1] or ""
        end
    end

    return main_target, kv_cmds
end

--return common
