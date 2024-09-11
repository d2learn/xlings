-- local common = require("common") -- ('require' is not callable)
import("common")
import("platform")

import("templates.c_language")
import("templates.cpp_language")
import("templates.py_language")

local book_folder_name = platform.get_config_info().bookdir

book_config_template = [[
[book]
title = "D2X Book Template | 动手学书籍模板"
author = "Author Name"
language = "en"

[build]
build-dir = "book"

[output.html]
git-repository-url = "https://github.com/Sunrisepeak/xlings"

[preprocessor.foo]
# Add any additional configurations
]]

function xlings_init_book()
    --os.exec("mkdir -p " .. book_folder_name)
    --os.exec("pwd")
    os.exec("mdbook init --force " .. book_folder_name)
    --os.exec("mdbook serve " .. book_folder_name)

    -- add .gitignore
    common.xlings_create_file_and_write(
        book_folder_name .. "/.gitignore",
        "book"
    )

    -- add book config file
    common.xlings_create_file_and_write(
        book_folder_name .. "/book.toml",
        book_config_template
    )

    os.exec("mdbook build -o " .. book_folder_name)

end

function xlings_init_exercises(xlings_name, xlings_lang)
    --os.exec("mkdir -p " .. xlings_name)
    --os.exec("mkdir -p " .. xlings_name .. "/exercises")
    --os.exec("mkdir -p " .. xlings_name .. "/tests")

    local file
    local x_template
    
    if xlings_lang == "cpp" then
        x_template = cpp_language.get_template()
    elseif xlings_lang == "python" then
        x_template = py_language.get_template()
    else
        x_template = c_language.get_template()
    end

    --print(c_language)
    --print(common)
    --print(x_template)

    xlings_name = platform.get_config_info().projectdir .. xlings_name

    -- add exercises file
    file = xlings_name .. "/" .. x_template.exercises_file
    common.xlings_create_file_and_write(
        file,
        x_template.exercises_file_template
    )

    print("[xlings]: " .. file .. " - ok")

    -- add tests file
    file = xlings_name .. "/" .. x_template.tests_file
    common.xlings_create_file_and_write(
        file,
        x_template.tests_file_template
    )

    print("[xlings]: " .. file .. " - ok")

    -- add build file
    file = xlings_name .. "/" .. x_template.build_file
    common.xlings_create_file_and_write(
        file,
        x_template.build_file_template
    )

    print("[xlings]: " .. file .. " - ok")
end

function xlings_init(xlings_name, xlings_lang)
    xlings_init_book()
    xlings_init_exercises(xlings_name, xlings_lang)
end