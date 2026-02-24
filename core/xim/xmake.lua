-- xim: standalone xmake task project
-- Supports both source tree (core/xim/) and release/package layout (xim/)
local xim_root
if os.isdir(path.join(os.projectdir(), "core", "xim")) then
    xim_root = path.join(os.projectdir(), "core", "xim")
else
    xim_root = path.join(os.projectdir(), "xim")
end
add_moduledirs(xim_root)
add_moduledirs(path.directory(xim_root))

task("xim")
    on_run(function ()
        import("core.base.option")
        local xim_entry = import("xim", {rootdir = xim_root, anonymous = true})
        local args = option.get("arguments") or { "-h" }
        xim_entry.main(table.unpack(args))
    end)
    set_menu{
        usage = "xmake xim [arguments]",
        description = "xim package manager",
        options = {
            {nil, "arguments", "vs", nil, "xim arguments"},
        }
    }

task("xscript")
    on_run(function ()
        import("core.base.option")
        local args = option.get("arguments") or {}
        local script_file = args[1]
        if not script_file or not os.isfile(script_file) then
            cprint("Usage: xmake xscript -- <script-file> [args...]")
            return
        end
        local script_dir = path.directory(script_file)
        local script_name = path.basename(script_file):gsub("%.lua$", "")
        local script_mod = import(script_name, {rootdir = script_dir, anonymous = true})
        local script_args = {}
        for i = 2, #args do
            table.insert(script_args, args[i])
        end
        if script_mod.xpkg_main then
            script_mod.xpkg_main(table.unpack(script_args))
        else
            cprint("[xlings]: no xpkg_main in %s", script_file)
        end
    end)
    set_menu{
        usage = "xmake xscript [arguments]",
        description = "run xpkg script",
        options = {
            {nil, "arguments", "vs", nil, "script file and arguments"},
        }
    }

