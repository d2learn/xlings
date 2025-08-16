import("common")

import("xim.libxpkg.system")
import("xim.base.utils")

function filepath_to_absolute(filepath)
    if not path.is_absolute(filepath) then
        filepath = path.join(system.rundir(), filepath)
    end
    return os.isfile(filepath), filepath
end

function try_download_and_check(url, dir, sha256)
    utils.try_download_and_check(url, dir, sha256)
end

function input_args_process(cmds_kv, args)
    return common.xlings_input_process("", args, cmds_kv)
end