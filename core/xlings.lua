import("core.base.text")
import("core.base.option")

import("checker")
import("init")

function xlings_help()
    cprint("${bright}xlings version:${clear} pre-v0.0.1")
    cprint("\n")
    cprint("${bright}Usage: $ ${cyan}xlings [command] [target]\n")
    cprint("${bright}Commands:${clear}")
    cprint("\t ${magenta}init${clear},    \t init projects by ${blue}config.xlings${clear}")
    cprint("\t ${magenta}checker${clear}, \t start auto-exercises from ${magenta}target${clear}")
    cprint("\t ${magenta}book${clear},    \t open book in your default browser")
    cprint("\t ${magenta}help${clear},    \t help info")
    cprint("\n")
    cprint("repo: ${underline}https://github.com/Sunrisepeak/xlings${clear}")
end

function main()
    local command = option.get("command")
    local start_target = option.get("start_target")
    local xlings_name = option.get("xlings_name")
    local xlings_lang = option.get("xlings_lang")

    --print(command)
    --print(start_target)
    --print(xlings_name)
    --print(xlings_lang)

    if command == "checker" or command == xlings_name then
        checker.main(start_target) -- TODO -s start_target
    elseif command == "init" then
        init.xlings_init(xlings_name, xlings_lang)
    elseif command == "book" then
        os.exec("mdbook build --open book") -- book is default folder
    else
        xlings_help()
    end
end