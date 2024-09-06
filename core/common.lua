import("net.http")
import("utils.archive")
import("platform")
import("devel.git")

--local common = {}

function xlings_create_file_and_write(file, context)
    local file, err = io.open(file, "w")

    if not file then
        print("Error opening file: " .. err)
        return
    end

    file:write(context)
    file:close()
end

function xlings_file_append(file, context)
    local file, err = io.open(file, "a")

    if not file then
        print("Error opening file: " .. err)
        return
    end

    file:write(context)
    file:close()
end

function xlings_path_format(path)
    path = string.gsub(path, "%.%.%/", "")
    return path:gsub("%.%.%\\", "")
end

function xlings_install_dependencies()
    local release_url
    local mdbook_zip
    local mdbook_bin
    local install_dir = platform.get_config_info().install_dir

    cprint("[xlings]: install mdbook...")

    release_url = platform.get_config_info().mdbook_url

    if is_host("windows") then
        mdbook_bin = install_dir .. "/bin/mdbook.exe"
        if not os.isfile(mdbook_bin) then
            mdbook_zip = install_dir .. "/mdbook.zip"
        end
    else
        mdbook_bin = install_dir .. "/bin/mdbook"
        if not os.isfile(mdbook_bin) then
            mdbook_zip = install_dir .. "/mdbook.tar.gz"
        end
    end

    if mdbook_zip then
        cprint("[xlings]: downloading mdbook from %s to %s", release_url, mdbook_zip)
        http.download(release_url, mdbook_zip)
        archive.extract(mdbook_zip, install_dir .. "/bin/")
        os.rm(mdbook_zip)
    end

    if os.isfile(mdbook_bin) then
        cprint("[xlings]: mdbook installed")
    end

end

function xlings_install()

    xlings_uninstall() -- TODO: avoid delete mdbook?

    cprint("[xlings]: install xlings to %s", platform.get_config_info().install_dir)

    local install_dir = platform.get_config_info().install_dir
    -- cp xliings to install_dir and force overwrite
    os.cp(platform.get_config_info().sourcedir, install_dir, {force = true})

    -- add bin to linux bashrc and windows's path env
    cprint("[xlings]: add bin to linux's .bashrc or windows's path env")

    if is_host("linux") then
        local bashrc = os.getenv("HOME") .. "/.bashrc"
        local context = "export PATH=$PATH:" .. install_dir .. "/bin"
        -- append to bashrc when not include xlings str in .bashrc
        if not os.isfile(bashrc) then
            xlings_create_file_and_write(bashrc, context)
        else
            local bashrc_content = io.readfile(bashrc)
            if not string.find(bashrc_content, context) then
                xlings_file_append(bashrc, context)
            end
        end
    else
        local path_env = os.getenv("PATH")
        if not string.find(path_env, install_dir) then

            path_env = path_env .. ";" .. install_dir
            -- os.setenv("PATH", path_env) -- only tmp, move to install.win.bat
            -- os.exec("setx PATH " .. path_env)
        end
    end

    xlings_install_dependencies()
end

function xlings_uninstall()
    local install_dir = platform.get_config_info().install_dir
    try
    {
        function()
            os.rm(install_dir)
        end,
        catch
        {
            function (e)
                -- TODO: error: cannot remove directory C:\Users\Public\xlings Unknown Error (145)
            end
        }
    }

    cprint("xlings uninstalled(" .. install_dir .. ") - ok")

end

function xlings_update()
    cprint("[xlings]: try to update xlings...")
    local install_dir = platform.get_config_info().install_dir
    git.pull({remote = "origin", tags = true, branch = "main", repodir = install_dir})
end

--return common