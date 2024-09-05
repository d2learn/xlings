import("core.project.project")
import("core.project.target")
import("core.base.global")
import("core.base.option")

import("platform")

-- TODO: optimze
target_to_code_file = { }

XLINGS_WAIT = "XLINGS_WAIT"
XLINGS_RETURN = "XLINGS_RETURN"

function clear_screen()
--[[
    if os.host() == "windows" then
        os.exec("xligns_clear.bat")
    else
        os.exec("clear")
    end
]]
    os.exec(platform.get_config_info().cmd_clear)
end

function print_info(target_name, built_targets, total_targets, current_file_path, output, status)

    clear_screen()

    -- print progress_bar
    local progress_bar_length = total_targets
    local arrow_count = built_targets
    local dash_count = progress_bar_length - arrow_count
    local progress_bar = string.format(
        "🌏Progress: [%s>%s] %d/%d",
        string.rep("=", arrow_count),
        string.rep("-", dash_count),
        arrow_count, progress_bar_length
    )
    print(progress_bar)

    print(string.format("\n[Target: %s]\n", target_name))

    -- print status
    if status then
        print(string.format("✅ Successfully ran %s!", current_file_path))
        print("\n🎉   The code is compiling!   🎉\n")
    else
        print(string.format("❌ Error: Compilation/Running failed for %s", current_file_path))
        print("\n The code exist some error!\n")
    end

    -- print output
    print("Output:")
    print("====================")
    print(output)
    print("====================")

    print("\nHomepage: https://github.com/d2learn/xlings")
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

    --clear_screen()

    local base_dir = os.projectdir()

    print(base_dir)

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
    for _, name in pairs(sorted_targets) do

        if skip and (name == start_target or string.find(name, start_target)) then
            skip = false;
        end

        if not skip then
            local files = targets[name]:sourcefiles()
            for  _, file in ipairs((files)) do
                local relative_path = path.relative(file, base_dir)
                local build_success = false
                local sleep_sec = 1000 * 0.1
                local output = ""

                local file_modify_time
                local compile_bypass_counter = 0

                while not build_success do
                    local curr_file_mtime = os.mtime(file)
                    if target_to_code_file[name] then
                        curr_file_mtime = curr_file_mtime + os.mtime(target_to_code_file[name])
                    end

                    if file_modify_time ~= curr_file_mtime then
                        --build_success = task.run("build", {target = name})
                        build_success = true

                        output, build_success = build_with_error_handling(name)

                        if build_success then
                            output, build_success = run_with_error_handling(name)
                        end

                        local status = build_success

                        if type(output) == "string" then
                            if string.find(output, "❌") then
                                status = false
                                build_success = false
                            elseif string.find(output, XLINGS_WAIT) or string.find(output, XLINGS_RETURN) then
                                build_success = false
                            end
                        end

                        if build_success then
                            built_targets = built_targets + 1
                        else
                            sleep_sec = 1000 * 1
                        end

                        print_info(name, built_targets, total_targets, relative_path, output, status)
                        output = ""
                    else
                        compile_bypass_counter = compile_bypass_counter + 1
                    end

                    file_modify_time = curr_file_mtime
                    if compile_bypass_counter > 20 then
                        compile_bypass_counter = 0
                        file_modify_time = nil
                    end
                    os.sleep(sleep_sec)

                end
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

    print_info("xlings", total_targets, total_targets, "...", bingo, true)

end