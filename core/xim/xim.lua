import("CmdProcessor")

function _tests()
--[[
    print("\n[XIM-TEST]: help")
    CmdProcessor.new(nil, {"-h"}):run()

    print("\n[XIM-TEST]: list")
    CmdProcessor.new("", {"-l"}):run()
]]
    print("\n[XIM-TEST]: install")
    CmdProcessor.new("vscode@1.93.1@microsoft"):run()
end

-- target name@version@maintainer
-- args: -i -r -u -l -s -h -y -v
function main(target, args)
    _tests()
end