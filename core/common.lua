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

--return common