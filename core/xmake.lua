add_rules("mode.debug")

add_moduledirs(".")

if xlings_name == nil then
    xlings_name = "xlings_name"
end

if default_start_target == nil then
    default_start_target = "lings"
end

task("xlings")
    on_run("xlings")
    set_menu{
        usage = "xmake xlings [options]",
        description = "init | checker | help",
        options = {
            {nil, "command", "v", nil, "xlings's command"},
        }
    }

task(xlings_name)
    on_run("xlings")
    set_menu{
        usage = "xmake" .. xlings_name .. "[options] [arguments]",
        description = "exercises-code compile-time & runtime checker",
        options = {
            {'s', "start_target", "kv", default_start_target, "check from start_target"},
        }
    }