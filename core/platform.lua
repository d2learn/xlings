local xlings_install_dir = {
    linux = ".xlings",
    windows = "C:/Users/Public/xlings",
}

-- v0.4.40
local xlings_mdbook_url = {
    linux = "https://github.com/rust-lang/mdBook/releases/download/v0.4.40/mdbook-v0.4.40-x86_64-unknown-linux-gnu.tar.gz",
    windows = "https://github.com/rust-lang/mdBook/releases/download/v0.4.40/mdbook-v0.4.40-x86_64-pc-windows-msvc.zip",
}

local command_clear = {
    linux = "clear",
    windows = xlings_install_dir.windows .. "/tools/xlings_clear.bat",
}

local command_wrapper = {
    linux = "",
    windows = xlings_install_dir.windows .. "/tools/win_cmd_wrapper.bat ",
}

local xlings_sourcedir = os.scriptdir() .. "/../"
local xlings_projectdir = "../" -- xlings_rundir TODO: optimize
local xlings_bookdir = xlings_projectdir .. "book/"
local xlings_drepodir = xlings_sourcedir .. "drepo/"

if os.host() == "linux" then
    xlings_install_dir.linux = os.getenv("HOME") .. "/" .. xlings_install_dir.linux
end

-- user config.xlings
-- Note: need init in xlings.lua
local xlings_name
local xlings_rundir
local xlings_cachedir = xlings_projectdir .. ".xlings/"
local xlings_editor

function set_name(name)
    xlings_name = name
end

function set_rundir(rundir)
    xlings_rundir = rundir
    xlings_cachedir = rundir .. "/.xlings/"
end

function set_editor(editor)
    xlings_editor = editor
end

-- llm config

local llm_id = "tongyi"
local llm_key = "sk-xxx"
local llm_system_bg = [[
背景: 你是一个代码专家
任务: 进行代码报错相关内容的提示和建议
输出要求: 用时而可爱、时而傲娇的方式回答, 并且每次回答不超过100字
示例:
    输入: 代码运行时报错：未定义变量。
    输出: 哎呀，小变量迷路啦！检查一下变量名是不是写错了呢?🎈
    输入: 代码运行时报错：未定义变量。
    输出: 哼，变量都找不到！快去检查你的拼写吧，本天才才不会轻易原谅呢！🌟
]]
local llm_outputfile = "llm_response.xlings.json"
local llm_run_status = false
local llm_response = "..."
local llm_enable = false
local llm_request_counter = 0

function set_llm_id(id)
    if id then
        llm_id = id
    end
end

function set_llm_key(key)
    if key then
        llm_key = key
        llm_enable = true
    end
end

function set_llm_system_bg(system_bg)
    if system_bg then
        llm_system_bg = system_bg
    end
end

function set_llm_outputfile(outputfile)
    llm_outputfile = outputfile
end

function set_llm_run_status(run_status)
    llm_run_status = run_status
end

function set_llm_response(response)
    llm_response = response
end

function add_llm_request_counter()
    llm_request_counter = llm_request_counter + 1
end

-- features config

local xlings_runmode = "normal" -- normal, loop

function set_runmode(runmode)
    xlings_runmode = runmode
end

-- all config info

function get_config_info()
    return {
        install_dir = xlings_install_dir[os.host()],
        sourcedir = xlings_sourcedir,
        mdbook_url = xlings_mdbook_url[os.host()],
        cmd_clear = command_clear[os.host()],
        cmd_wrapper = command_wrapper[os.host()],
        projectdir = xlings_projectdir,
        drepodir = xlings_drepodir,
        rundir = xlings_rundir,
        bookdir = xlings_bookdir,
        cachedir = xlings_cachedir,
        editor = xlings_editor,
        name = xlings_name,
        runmode = xlings_runmode,
        llm_config = {
            id = llm_id,
            key = llm_key,
            system_bg = llm_system_bg,
            outputfile = xlings_cachedir .. llm_outputfile,
            run_status = llm_run_status,
            response = llm_response,
            enable = llm_enable,
            request_counter = llm_request_counter,
        },
    }
end

