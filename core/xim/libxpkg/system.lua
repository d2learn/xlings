import("platform")

import("xim.base.runtime")

function xpkg_args()
    return runtime.get_runtime_data().input_args.sysxpkg_args
end

function rundir()
    return runtime.get_rundir()
end

function bindir()
    return runtime.get_bindir()
end