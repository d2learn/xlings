import("core.project.project")
import("core.project.target")
import("core.base.global")
import("core.base.option")
import("core.base.fwatcher")
import("lib.detect.find_tool")

import("private.async.jobpool")
import("async.runjobs")

import("common")
import("platform")
import("llm.llm_interface")

-- TODO: optimze
target_to_code_file = {}

-- don't use this, deprecated
XLINGS_WAIT = "XLINGS_WAIT"
XLINGS_RETURN = "XLINGS_RETURN"

-- d2x flag
D2X_WAIT = "D2X_WAIT"

-- remove ../ or ..\
function delete_path_prefix(path)
    path = tostring(path)
    if "windows" == os.host() then
        path = path:gsub("%.%.%\\", "")
    else
        path = string.gsub(path, "%.%.%/", "")
    end
    -- remove leading and trailing whitespaces
    path = string.gsub(path, "^%s*(.-)%s*$", "%1")
    return path
end

-- TODO: support user to define editor_opencmd_template function
local __nvim_server_started = false
local __nvim_server_address = "127.0.0.1:43210"
local __default_editor_opencmd_template = nil
function default_editor_opencmd_template(editor)

    if __default_editor_opencmd_template then
        return __default_editor_opencmd_template
    end

    if editor == "vscode" then
        __default_editor_opencmd_template = [[ code -g "%s:1"]]
    elseif editor == "nvim" or editor == "vim" then
        if not __nvim_server_started then
            local terminal_app = nil
            if is_host("linux") then
                if find_tool("gnome-terminal") then
                    terminal_app = "gnome-terminal -- "
                elseif find_tool("kitty") then
                    terminal_app = "kitty "
                elseif find_tool("alacritty") then
                    terminal_app = "alacritty -e "
                elseif find_tool("konsole") then
                    terminal_app = "konsole -e "
                end
            elseif is_host("windows") then
                -- cmd or window terminal gui window
                -- terminal_app = "wt -w newcmd cmd /k " -- `help windows / tips` issues
                terminal_app = "cmd /c start "
            else
                -- TODO: macosx
            end

            if terminal_app then
                os.exec(terminal_app .. string.format([[ nvim --listen %s ]], __nvim_server_address))
                os.sleep(500) -- wait nvim opened
            end

            __nvim_server_started = true
        end

        __default_editor_opencmd_template = string.format([[nvim --server %s ]], __nvim_server_address)
            .. [[ --remote-send ":edit %s<CR>"]]
    elseif editor == "zed" then
        __default_editor_opencmd_template = [[ zed "%s:1"]]
    else
        -- TODO3: support more editor?
    end

    return __default_editor_opencmd_template
end

function print_info(target_name, built_targets, total_targets, target_files, output, status)
    -- TODO: optimize / workaround cls failed on windows
    common.xlings_clear_screen()
    io.stdout:flush()
    common.xlings_clear_screen()

    local config = platform.get_config_info()
    local current_file_path_old = target_files[#target_files]
    local current_file_path = delete_path_prefix(current_file_path_old)

    -- format path(remove prefix) for output
    output = tostring(output):gsub(current_file_path_old, current_file_path)
    -- becuase d2x is running in the .xlings cache directory
    if config.d2x and output then
        output = output:replace([[../]] .. config.d2x.checker.name, config.d2x.checker.name)
            :replace([[..\]] .. config.d2x.checker.name, config.d2x.checker.name)
    end

    -- print progress_bar
    local progress_bar_length = total_targets
    local arrow_count = built_targets
    local dash_count = progress_bar_length - arrow_count
    local progress_bar = string.format(
        "🌏Progress: ${green}[%s>%s]${clear} %d/%d",
        string.rep("=", arrow_count),
        string.rep("-", dash_count),
        arrow_count, progress_bar_length
    )
    cprint(progress_bar)

    cprint(
        string.format("\n[Target: %s] - ", target_name) ..
        "${blink bright magenta}" .. config.runmode .. "${clear}\n"
    )

    -- print status
    if status then
        print(string.format("✅ Successfully ran %s", current_file_path))
        print("\n🎉   The code is compiling!   🎉\n")
    else
        print(string.format("❌ Error: Compilation/Running failed for %s", current_file_path))
        print("\n The code exist some error!\n")
        -- TODO: remove the path prefix for output info
        --if type(output) == "string" then
        --output = common.xlings_path_format(output)
        --end
    end

    -- print output
    cprint("${dim bright}---------C-Output---------${clear}")
    print(output)

    if target_name ~= config.name or built_targets ~= total_targets then
        -- print ai tips
        local llm_config = config.llm_config
        if llm_config.enable then
            cprint("\n${blink bright cyan}AI-Tips(" .. llm_config.request_counter .. "):${clear}")
            cprint(llm_config.response)
        else
            cprint("\n${dim cyan}AI-Tips-Config:${clear} ${dim underline}https://d2learn.org/docs/xlings${clear}")
        end

        -- print files detail
        cprint("\n${dim bright}---------E-Files---------${clear}")
        local files_detail = ""
        for _, file in ipairs(target_files) do
            files_detail = files_detail .. file .. "\n"
        end
        print(delete_path_prefix(files_detail))
    end

    cprint("${dim bright}-------------------------${clear}")
    cprint("\nHomepage: ${underline}https://github.com/d2learn/xlings${clear}")
end

function build_with_error_handling(target)
    local output, err
    local build_success = true

    try
    {
        function()
            os.iorunv("xmake", { "build", target })
            --os.iorunv("xmake", {"build", "-v", name})
        end,
        catch
        {
            -- After an exception occurs, it is executed
            function(e)
                output = e
                build_success = false
            end
        }
    }

    return output, build_success
end

-- TODO: optimze capture stdout + stderr
function run_with_error_handling(target)
    local output, err
    local run_success = true

    try {
        function()
            output, err = os.iorunv("xmake", { "r", target }, { timeout = 2000 })
        end,
        catch
        {
            function(e)
                output = e.stdout .. e.stderr -- .. e.errors
                run_success = false
            end
        },
        [[--
        finally {
            function (ok, outdata, errdata)
                output =  outdata .. errdata
            end
        }
--]]
    }

    return output, run_success
end

-- main start
function main(start_target, opt)
    opt = opt or {}

    local checker_pass = false
    --local start_target = option.get("start_target")

    if not start_target then start_target = "" end -- TODO: optimize

    -- xlings project's cache dir(xmake.lua)
    local base_dir = os.projectdir()
    os.cd(base_dir) -- avoid other module's side effect

    local config = platform.get_config_info()
    local detect_dir = config.rundir
    local detect_recursion = false
    if config.d2x then -- is d2x project
        detect_dir = path.join(detect_dir, config.d2x.checker.name)
        detect_recursion = true
    else -- is xrun - TODO: optimize
        --detect_recursion = false
        if start_target == "" then os.raise("target is empty, please run in d2x-project's root directory") end
        local files = project.targets()[start_target]:sourcefiles()
        if #files > 0 then
            local file = path.absolute(files[1])
            detect_dir = path.directory(file)
        end
    end

    common.xlings_clear_screen()

    fwatcher.add(detect_dir, { recursion = detect_recursion })
    --cprint("Watching directory: ${magenta}" .. detect_dir .. "${clear}")

    -- 获取 project 中所有 target
    local targets = project.targets()
    --local total_targets = #targets --
    -- TODO: optimize total_targets
    local total_targets = 0
    local built_targets = 0
    local sorted_targets = {}
    for name, _ in pairs(targets) do
        total_targets = total_targets + 1
        table.insert(sorted_targets, name)
    end

    table.sort(sorted_targets)

    local skip = true
    local runmode = platform.get_config_info().runmode

    for _, name in pairs(sorted_targets) do
        if skip and (name == start_target or string.find(name, start_target, 1, true)) then
            skip = false;
        end

        if not skip then
            local files = targets[name]:sourcefiles()
            local build_success = false
            local output = ""
            local old_output_llm_req = ""
            local relative_path = ""

            local compile_bypass_counter = 21
            local open_target_file = false
            local update_ai_tips = false
            local file_detect_interval = 0
            local status = false

            while not build_success do
                local llm_config = platform.get_config_info().llm_config
                local ok = 0
                local event = nil

                if file_detect_interval > 0 then
                    ok, event = fwatcher.wait(file_detect_interval)
                end

                --print(ok)
                --print(compile_bypass_counter)

                local checker_task = function()
                    if ok > 0 or compile_bypass_counter > 20 then
                        if targets[name]:get("kind") ~= "phony" then
                            output, build_success = build_with_error_handling(name)
                        else
                            build_success = true
                        end

                        if build_success then
                            output, build_success = run_with_error_handling(name)
                        end

                        status = build_success

                        if type(output) == "string" then
                            if string.find(output, "❌", 1, true) then
                                status = false
                                build_success = false
                                -- TODO: optimize, remove XLINGS_*
                            elseif string.find(output, XLINGS_WAIT) or string.find(output, XLINGS_RETURN) or string.find(output, D2X_WAIT) then
                                build_success = false
                            end
                        end

                        if runmode == "loop" then
                            build_success = false -- continue to run
                        end

                        if build_success then
                            built_targets = built_targets + 1
                        elseif opt.editor and open_target_file == false then

                            local open_file_cmd_template = default_editor_opencmd_template(opt.editor)

                            if open_file_cmd_template then
                                for _, file in ipairs((files)) do
                                    file = path.absolute(file)
                                    common.xlings_run_in_script(string.format(open_file_cmd_template, file))
                                end
                            end

                            open_target_file = true

                        end

                        print_info(name, built_targets, total_targets, files, output, status)
                        compile_bypass_counter = 0
                    else
                        if update_ai_tips then
                            print_info(name, built_targets, total_targets, files, output, status)
                            update_ai_tips = false
                        end
                        compile_bypass_counter = compile_bypass_counter + 1
                    end
                end

                local llm_task = function()
                    --print(llm_config)
                    if llm_config.enable and llm_config.run_status == false then
                        -- TODO:
                        --      rebuild when haven't change code
                        --      old_output_llm_req ~= output will be to true ?
                        if old_output_llm_req ~= output then
                            if compile_bypass_counter >= 1 and output then
                                local sourcecode = "[sourcecode]: \n"
                                for _, file in ipairs((files)) do
                                    sourcecode = sourcecode .. "--" .. file .. "\n"
                                    file = path.absolute(file)
                                    local file_content = io.readfile(file)
                                    sourcecode = sourcecode .. file_content .. "\n"
                                end
                                local llm_request = sourcecode .. "\n\n [compiler output]: \n" .. output
                                llm_interface.send_request(llm_request)
                                old_output_llm_req = output
                            else
                                platform.set_llm_response("...")
                            end
                            update_ai_tips = true
                        else
                            --platform.set_llm_response(2 * compile_bypass_counter .. "...")
                            --update_ai_tips = true
                        end
                    end
                end

                local jobs = jobpool.new()
                jobs:addjob("xlings/checker", checker_task, { progress = true })
                jobs:addjob("xlings/llm", llm_task, { isolate = true })
                runjobs("xlings-task", jobs, { parallel = true })

                file_detect_interval = 1500
            end
        else
            built_targets = built_targets + 1
        end
    end

    local bingo = "\
        Bingo! 🎉🎉🎉\n \
You have completed all exercises\n \
tools-repo: https://github.com/d2learn/xlings\
"
    config = platform.get_config_info()
    print_info(config.name, total_targets, total_targets, { "config.xlings" }, bingo, true)
end
