xpack("xlings")
    set_homepage("https://github.com/d2learn/xlings")
    set_version("0.0.1")
    set_title("xlings build utility ($(arch))")
    set_description("A cross-platform build utility for similar d2x project.")
    set_copyright("Copyright (C) 2024-present, d2learn group")
    set_author("sunrisepeak")
    set_maintainer("sunrisepeak & d2learn")
    set_license("Apache-2.0")
    set_licensefile("../LICENSE")
    set_company("github.com/d2learn")

    set_formats("nsis", "deb")
    set_basename("xlings-pre-v$(version)")

    --add_sourcefiles("../(tools/*)", {prefixdir = "tools"})

    on_installcmd(function (package, batchcmds)
        local format = package:format()
        if format == "nsis" then
            --batchcmds:runv("tools/install.win.bat")
        else
            --batchcmds:runv("./tools/install.unix.sh")
        end
    end)

    xpack_component("vscode")

xpack_component("vscode")
    set_default(false)
    set_title("vscode")