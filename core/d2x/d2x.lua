import("d2x.actions")

function help()
    print("Usage: d2x <action> [args]")
end

function main(action, args)
    if action == "init" then
        print("TODO: init")
    elseif action == "book" then
        print("TODO: book")
    elseif action == "run" then
        actions.run(args)
    elseif action == "checker" then
        actions.checker(args)
    else
        help()
    end
end