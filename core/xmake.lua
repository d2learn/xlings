add_rules("mode.debug")

add_moduledirs(".")

if xname == nil then
    xname = "xlings_name"
end

if xlings_name then
    xname = xlings_name
end

if xlings_runmode == nil then
    xlings_runmode = "normal"
end

task("xlings")
    on_run("xlings")
    set_menu{
        usage = "xmake xlings [options]",
        description = "init | book | checker | help",
        options = {
            -- args - input
            {nil, "run_dir", "v", nil, "xlings's run directory"}, -- only internal use
            {nil, "command", "v", nil, "xlings's command"},
            {nil, "cmd_target", "v", nil, "check from start_target"},
            -- args - default
            {nil, "xname", "v", xname, "xlings's name"},
            {nil, "xdeps", "v", xdeps, "project dependencies auto install"},
            {nil, "xlings_lang", "v", xlings_lang, "xlings's programming languages"},
            {nil, "xlings_editor", "v", xlings_editor, "xlings's programming languages"},
            {nil, "xlings_llm_config", "v", xlings_llm_config, "xlings's llm config file path"},
            {nil, "xlings_runmode", "v", xlings_runmode, "xlings's run mode"},
        }
    }

includes("@builtin/xpack")
if xpack then
    includes("xpack.lua")
end