import("CmdProcessor")

function _tests()
--[[
    print("\n[XIM-TEST]: help")
    CmdProcessor.new(nil, {"-h"}):run()

    print("\n[XIM-TEST]: list")
    CmdProcessor.new("", {"-l"}):run()
]]
    CmdProcessor.new("mdbook@0.4.40", {}):run()
end

-- target name@version@maintainer
-- args: -i -r -u -l -s -h -y -v
function main(target, args)
    _tests()
end