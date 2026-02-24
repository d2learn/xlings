-- xim: standalone xmake task project (included by xlings via includes("core/xim/xmake.lua"))
local xim_root = path.join(os.projectdir(), "core", "xim")
add_moduledirs(xim_root)

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

