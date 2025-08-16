import("platform")
import("common")

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
                        log.warn("retry %d ${blink}...", opt.retry)
                        os.sleep(1000)
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

function unix_api()
    return {
        append_to_shell_profile = __unix_append_to_shell_profile,
    }
end

function __unix_append_to_shell_profile(config)

    if not config then
        log.warn("config is nil...")
        return
    end

    if type(config) == "string" then
        config = {
            fish = config,
            posix = config,
        }
    end

    local rcachedir = platform.get_config_info().rcachedir
    local posix_profile = path.join(rcachedir, "xlings-profile.sh")
    local fish_profile = path.join(rcachedir, "xlings-profile.fish")

    function config_shell(profile, shell_config)
        if os.isfile(profile) then
            cprint("[xlings]: add config to shell profile - %s", profile)
            -- backup
            os.cp(profile, profile .. ".xlings.bak", {force = true})
            local profile_content = io.readfile(profile)
            if not string.find(profile_content, shell_config, 1, true) then
                shell_config = "\n" .. shell_config
                common.xlings_file_append(profile, shell_config)
            else
                cprint("[xlings]: [%s] already configed", shell)
            end
        end
    end

    if config.posix then
        config_shell(posix_profile, config.posix)
    end

    if config.fish then
        config_shell(fish_profile, config.fish)
    end
end