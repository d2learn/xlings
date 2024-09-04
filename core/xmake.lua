add_rules("mode.debug")

add_moduledirs(".")

if xlings_name == nil then
    xlings_name = "xlings_name"
end

task("xlings")
    on_run("xlings")
    set_menu{
        usage = "xmake xlings [options]",
        description = "init | book | checker | help",
        options = {
            -- args - input
            {nil, "command", "v", nil, "xlings's command"},
            {nil, "start_target", "v", xlings_name, "check from start_target"},
            -- args - default
            {nil, "xlings_name", "v", xlings_name, "xlings's name"},
            {nil, "xlings_lang", "v", xlings_lang, "xlings's programming languages"},
        }
    }