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
            {nil, "command", "v", nil, "xlings's command"},
            {nil, "xlings_name", "v", xlings_name, "xlings's name"},
            {nil, "xlings_lang", "v", xlings_lang, "xlings's programming languages"},
        }
    }

task(xlings_name)
    on_run("xlings")
    set_menu{
        usage = "xmake" .. xlings_name .. "[options] [arguments]",
        description = "exercises-code compile-time & runtime checker",
        options = {
            {'s', "start_target", "kv", xlings_name, "check from start_target"},
        }
    }