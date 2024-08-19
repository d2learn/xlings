import("core.base.option")
import("checker")
import("init")

function xlings_help()
    print("xlings version: pre-v0.0.1")
end

function main()
    local command = option.get("command")
    local start_target = option.get("start_target")

    print(command)
    print(start_target)

    if start_target then
        checker.main(start_target)
    else
        if command == "checker" then
            checker.main("lings") -- TODO -s start_target
        elseif command == "init" then
            init.xlings_init()
        else
            xlings_help()
        end
    end
end