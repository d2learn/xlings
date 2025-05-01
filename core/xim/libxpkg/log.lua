function info(text, ...)
    __log(text, "green", ...)
end

function debug(text, ...)
    __log(text, "bright", ...)
end

function warn(text, ...)
    __log(text, "yellow", ...)
end

function error(text, ...)
    __log(text, "red", ...)
end

function __log(text, color, ...)
    if not text or debug_mode then
        return
    end
    local args = { color, ... }
    cprint([[${%s}[xim:xpkg]${clear}: ]] .. text, unpack(args))
end