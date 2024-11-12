function support()
    return {
        windows = true,
        linux = false,
        macosx = false
    }
end

function installed()
    return false
end

function install()
    print("[xlings]: Installing VC++ 6.0 ...")
    os.exec("xmake g -c")
    cprint(
        "${yellow}\n" ..
        "Warning: VC++6.0是一个近30年前的软件(1998), 与现实环境严重不符。建议使用下面环境进行替代\n\n" ..
        "\tDev-C++ \t| 界面简单, 适合初学 -> 安装命令:  xlings install devcpp\n" ..
        "\tVisual Studio \t| 专业开发, 功能强大 -> 安装命令:  xlings install vs\n" ..
        "${clear}"
    )

end