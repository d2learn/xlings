import("d2x.actions")

function help()
    print("Usage: d2x <action> [args]")
end

function main(action, ...)
    local args = {...} or { "" }

    if action == "init" then
        print("TODO: init")
    elseif action == "book" then
        print("TODO: book")
    elseif action == "run" then
        actions.run(args[1])
    elseif action == "checker" then
        actions.checker(args[1])
    else
        help()
    end
end