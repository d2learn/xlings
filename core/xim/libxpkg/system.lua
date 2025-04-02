import("xim.base.runtime")

function input_args()
    return runtime.get_pkginfo().input_args
end

function rundir()
    return runtime.get_rundir()
end