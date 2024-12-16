import("xim.xim")

-- only support uninstall
function main(target)
    xim("-r", target, "-y")
end