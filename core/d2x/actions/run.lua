import("common")
import("platform")

import("d2x.templates.templates_interface")

-- detect file type exa: .c .cpp .java .py .lua
function detect_file_type(source_file)
    local ext = string.match(source_file, "%.(%w+)$")
    if ext == "c" or ext == "cpp" then
        return "c"
    elseif ext == "java" then
        return "java"
    elseif ext == "py" then
        return "python"
    elseif ext == "lua" then
        return "lua"
    else
        return "unknown"
    end
end

function generate_config(target_name, target_sourcefile)

    local config = platform.get_config_info()
    local xlings_file = config.install_dir .. "/core/xmake.lua"
    local file_type = detect_file_type(target_sourcefile)
    local template = templates_interface.get_template(file_type)
    local xmake_file_path = config.cachedir .. "/xmake.lua"

    local content = string.format(
        template.xmake_file_template,
        file_type,
        target_name,
        "[[" .. target_sourcefile .. "]]",
        xlings_file
    )
    common.xlings_create_file_and_write(xmake_file_path, content)
end

function main(source_file)

    source_file = tostring(source_file)

    if os.isfile(tostring(source_file)) then
        source_file = path.absolute(source_file)
    else
        source_file = path.join(platform.get_config_info().rundir, source_file)
        if not os.isfile(source_file) then
            cprint("[xlings]: ${red}file not found${clear} - " .. source_file)
            return
        end
    end

    local target_name = path.basename(source_file)
    generate_config(target_name, source_file)
    --os.exec("pwd")
    os.cd(platform.get_config_info().cachedir)
    os.exec(
        "xmake xlings " ..
        platform.get_config_info().rundir ..
        " d2x checker " .. target_name
    )
end