import("lib.detect.find_tool")

import("common")
import("platform")

import("d2x.templates.c_language")
import("d2x.templates.cpp_language")
import("d2x.templates.py_language")

local __projectdir = nil
local __projectlang = nil
local __book_folder_name = nil
local __dslingsdir = nil

project_config_template = [[
xname = "%s" -- project name

-- xim-deps / xdeps (deprecated)
xim = {
    mdbook = "",
    vscode = "",
    %s = "", -- language
}

d2x = {
    checker = {
        name = "dslings",
        -- editor = "vscode",
    },
    private = {
        -- project private attributes
    }
}
]]

book_config_template = [[
# docs: https://rust-lang.github.io/mdBook
[book]
title = "D2X Book Template | 动手学书籍模板"
author = "Author Name"
language = "en"

[build]
build-dir = "book"

[output.html]
git-repository-url = "https://github.com/d2learn/xlings"

[preprocessor.foo]
# Add any additional configurations
]]

function init_project_base(name)
    local projectfile = path.join(__projectdir, "config.xlings")
    local project_ignore = path.join(__projectdir, ".gitignore")

    cprint("[xligns:d2x]: create [%s] project...", name)
    os.mkdir(__projectdir)

    cprint("[xligns:d2x]: init project config file...")
    common.xlings_create_file_and_write(
        projectfile,
        string.format(project_config_template, name, __projectlang)
    )

    local gitignore_content = [[

# auto generated by xlings(d2x)
.xlings

]]

    if os.isfile(project_ignore) then
        cprint("[xligns:d2x]: add .gitignore...")
        common.xlings_file_append(project_ignore, gitignore_content)
    else
        common.xlings_create_file_and_write(project_ignore, gitignore_content)
    end

end

function init_book()

    cprint("[xligns:d2x]: init book...")

    if not os.isdir(__book_folder_name) then
        os.mkdir(__book_folder_name)
    end

    os.iorun("mdbook init --force " .. __book_folder_name)
    --os.iorun("mdbook serve " .. __book_folder_name)

    -- add .gitignore
    cprint("[xligns:d2x]: add .gitignore...")
    common.xlings_create_file_and_write(
        path.join(__book_folder_name, ".gitignore"),
        "book"
    )

    -- add book config file
    cprint("[xligns:d2x]: add book.toml...")
    common.xlings_create_file_and_write(
        path.join(__book_folder_name, "book.toml"),
        book_config_template
    )

    os.iorun("mdbook build -o " .. __book_folder_name)

end

function init_exercises()
    cprint("[xligns:d2x]: init exercises...")

    local file
    local x_template

    if __projectlang == "cpp" then
        x_template = cpp_language.get_template()
    elseif __projectlang == "python" then
        x_template = py_language.get_template()
    else
        x_template = c_language.get_template()
    end

    --print(c_language)
    --print(common)
    --print(x_template)

    -- add exercises file
    file = path.join(__dslingsdir, x_template.exercises_file)
    common.xlings_create_file_and_write(
        file,
        x_template.exercises_file_template
    )

    -- add tests file
    file = path.join(__dslingsdir, x_template.tests_file)
    common.xlings_create_file_and_write(
        file,
        x_template.tests_file_template
    )

    -- add build file
    file = path.join(__dslingsdir, x_template.build_file)
    common.xlings_create_file_and_write(
        file,
        x_template.build_file_template
    )

end

function main(name, dslings, lang)

    name = name or "d2x-demo"
    dslings = dslings or "dslings"
    lang = lang or "c"

    local rundir = platform.get_config_info().rundir or os.curdir()

    __projectdir = path.join(rundir, name)
    __projectlang = lang
    __book_folder_name = path.join(__projectdir, "book")
    __dslingsdir = path.join(__projectdir, dslings)

    if not find_tool("mdbook") then
        os.exec("xim mdbook -y")
    end

    init_project_base(name)
    init_book()
    init_exercises()

    cprint("[xligns:d2x]: ${green}%s${clear} | ${yellow}%s${clear}", name, __projectdir)
end