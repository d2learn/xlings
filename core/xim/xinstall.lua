-- for package file script
-- example:
--  import("xinstall")
--  xinstall("vscode")

import("xim")

-- only support install
function main(target)
    xim("-i", target, "-y", "--disable-info")
end