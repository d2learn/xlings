import("core.project.project")
import("core.project.target")
import("core.base.global")
import("core.base.option")
import("core.base.fwatcher")

import("private.async.jobpool")
import("async.runjobs")

import("common")
import("platform")
import("llm.llm_interface")

-- TODO: optimze
target_to_code_file = { }

XLINGS_WAIT = "XLINGS_WAIT"
XLINGS_RETURN = "XLINGS_RETURN"

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

function print_info(target_name, built_targets, total_targets, target_files, output, status)

    -- TODO: optimize / workaround cls failed on windows
    common.xlings_clear_screen()
    common.xlings_clear_screen()

    local config = platform.get_config_info()
    local current_file_path_old = target_files[#target_files]
    local current_file_path = delete_path_prefix(current_file_path_old)

    -- format path(remove prefix) for output
    output = tostring(output):gsub(current_file_path_old, current_file_path)
    -- becuase xchecker is running in the .xlings cache directory
    if config.xchecker and output then
        output = output:gsub(path.join("%.%.\\", config.name), config.name)
            :gsub(path.join("%.%./", config.name), config.name)
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
            cprint("\n${blink bright cyan}AI-Tips(" .. llm_config.request_counter  .. "):${clear}")
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
            os.iorunv("xmake", {"build", target})
            --os.iorunv("xmake", {"build", "-v", name})
        end,
        catch
        {
            -- After an exception occurs, it is executed
            function (e)
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
        function ()
            output, err = os.iorunv("xmake", {"r", target}, {timeout = 2000})
        end,
        catch
        {
            function (e)
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
function main(start_target)
    local checker_pass = false
    --local start_target = option.get("start_target")

    if start_target == nil then start_target = "" end -- TODO: optimize

    common.xlings_clear_screen()

    -- xlings project's cache dir(xmake.lua)
    local base_dir = os.projectdir()
    os.cd(base_dir) -- avoid other module's side effect

    local config = platform.get_config_info()
    local detect_dir = config.rundir
    local detect_recursion = false
    if config.xchecker then -- is d2x project
        detect_dir = path.join(detect_dir, config.xchecker.name)
        detect_recursion = true
    else -- is xrun - TODO: optimize
        --detect_recursion = false
        local files = project.targets()[start_target]:sourcefiles()
        if #files > 0 then
            local file = path.absolute(files[1])
            detect_dir = path.directory(file)
        end
    end
    fwatcher.add(detect_dir, {recursion = detect_recursion})
    --cprint("Watching directory: ${magenta}" .. detect_dir .. "${clear}")

    --if platform.get_config_info().editor == "vscode" then
        --os.exec("code " .. detect_dir)
    --else
        -- TODO3: support more editor?
    --end

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
                            elseif string.find(output, XLINGS_WAIT) or string.find(output, XLINGS_RETURN) then
                                build_success = false
                            end
                        end

                        if runmode == "loop" then
                            build_success = false -- continue to run
                        end

                        if build_success then
                            built_targets = built_targets + 1
                        else
                            -- TODO1: -> TODO-X
                            -- TODO2: skip to file-line? code -g file:line
                            if platform.get_config_info().editor == "vscode" then
                                if open_target_file == false then
                                    for  _, file in ipairs((files)) do
                                        file = path.absolute(file)
                                        os.exec(platform.get_config_info().cmd_wrapper .. "code -g " .. file .. ":1")
                                    end
                                    open_target_file = true
                                end
                            else
                                -- TODO3: support more editor?
                            end
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
                                llm_interface.send_request(output)
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
                jobs:addjob("xlings/checker", checker_task, {progress = true})
                jobs:addjob("xlings/llm", llm_task, {isolate = true})
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
    print_info(config.name, total_targets, total_targets, {"config.xlings"}, bingo, true)

end