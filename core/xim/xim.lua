import("CmdProcessor")

-- adjust the input parameters -> target arg1 arg2 arg3 ...
function _input_params_process(target, args)

    if target:sub(1, 1) == '-' then
        local new_target_index = nil
        local old_target = target
        target = ""
        for i = 1, #args do
            if args[i]:sub(1, 1) ~= '-' then
                target = args[i]
                new_target_index = i
                break
            end
        end

        if new_target_index then
            table.remove(args, new_target_index)
        end
        table.insert(args, 1, old_target)
    end

    return target, args
end

function _tests()
--[[
    print("\n[XIM-TEST]: help")
    CmdProcessor.new(nil, {"-h"}):run()

    print("\n[XIM-TEST]: list")
    CmdProcessor.new("", {"-l"}):run()
]]
    CmdProcessor.new("vscode", {"-i"}):run()
end

-- target name@version@maintainer
-- args: -i -r -u -l -s -h -y -v
-- example:
--      xmake l xim.lua -h
--      xmake l xim.lua vscode
--      xmake l xim.lua vscode -i
--      xmake l xim.lua -r vscode -y
function main(target, ...)
    local xim_args = { ... }
    try {
        function ()
            local new_target, args = _input_params_process(target, xim_args)
            --print("new_target: ", new_target)
            --print("args: ", args)
            CmdProcessor.new(new_target, args):run()
        end,
        catch {
            function (errors)
                print("[xlings:xim] main: error - ", errors)
            end
        }
    }
end
