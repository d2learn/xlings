--- xim api

import("xim.xim")

function install(target)
    xim("-i", target, "-y", "--disable-info")
end

function remove(target)
    xim("-r", target, "-y", "--disable-info")
end

function uninstall(target)
    remove(target)
end