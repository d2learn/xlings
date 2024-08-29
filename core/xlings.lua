import("core.base.option")
import("checker")
import("init")

function xlings_help()
    print("xlings version: pre-v0.0.1")
end

function main()
    local command = option.get("command")
    local xlings_name = option.get("xlings_name")
    local xlings_lang = option.get("xlings_lang")
    local start_target = option.get("start_target")

    print(command)
    print(xlings_name)
    print(xlings_lang)
    print(start_target)

    if start_target then
        checker.main(start_target)
    else
        if command == "checker" then
            checker.main("lings") -- TODO -s start_target
        elseif command == "init" then
            init.xlings_init(xlings_name, xlings_lang)
        elseif command == "book" then
            os.exec("mdbook build --open book") -- book is default folder
        else
            xlings_help()
        end
    end
end