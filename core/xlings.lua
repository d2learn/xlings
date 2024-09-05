import("core.base.text")
import("core.base.option")

import("common")
import("checker")
import("init")

function xlings_help()
    cprint("${bright}xlings version:${clear} pre-v0.0.1")
    cprint("")
    cprint("${bright}Usage: $ ${cyan}xlings [command] [target]\n")
    cprint("${bright}Commands:${clear}")
    cprint("\t ${magenta}init${clear},     \t init projects by ${blue}config.xlings${clear}")
    cprint("\t ${magenta}checker${clear},  \t start auto-exercises from ${magenta}target${clear}")
    cprint("\t ${magenta}book${clear},     \t open book in your default browser")
    cprint("\t ${magenta}update${clear},   \t update xlings to the latest version")
    cprint("\t ${magenta}uninstall${clear},\t uninstall xlings")
    cprint("\t ${magenta}help${clear},     \t help info")
    cprint("")
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
    elseif command == "update" then
        common.xlings_update(xlings_name, xlings_lang)
        xlings_help()
    elseif command == "install" then
        -- TODO: isn't user command, only use in install script
        common.xlings_install()
    elseif command == "uninstall" then
        common.xlings_uninstall()
    else
        xlings_help()
    end
end