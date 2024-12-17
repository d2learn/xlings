-- for package file script
-- example:
--  import("xim.xinstall")
--  xinstall("vscode")

import("xim.xim")

-- only support install
function main(target)
    xim("-i", target, "-y", "--disable-info")
end