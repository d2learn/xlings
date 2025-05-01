import("platform")

import("xim.base.runtime")

import("xim.libxpkg.log")

function xpkg_args()
    return runtime.get_runtime_data().input_args.sysxpkg_args
end

function rundir()
    return runtime.get_rundir()
end

function bindir()
    return runtime.get_bindir()
end

function exec(cmd, opt)
    local ok = false

    opt = opt or {}
    if opt.retry then
        while opt.retry > 0 and not ok do
            ok = try {
                function()
                    os.exec(cmd)
                    return true
                end,
                catch {
                    function(e)
                        print(e)
                        log.warn("retry %d", opt.retry)
                        opt.retry = opt.retry - 1
                        return false
                    end
                }
            }
        end
    end

    if not ok then
        os.exec(cmd)
    end
end