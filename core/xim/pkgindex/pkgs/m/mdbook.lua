package = {
    -- base info
    name = "mdbook",
    description = "Create book from markdown files. Like Gitbook but implemented in Rust",

    authors = "Mathieu David, Michael-F-Bryan, Matt Ickstadt",
    contributor = "https://github.com/rust-lang/mdBook/graphs/contributors",
    license = "MPL-2.0",
    repo = "https://github.com/rust-lang/mdBook",
    docs = "https://rust-lang.github.io/mdBook",

    -- xim pkg info
    archs = {"x86_64"},
    status = "stable", -- dev, stable, deprecated
    categories = {"book", "markdown"},
    keywords = {"book", "gitbook", "rustbook", "markdown"},

    xpm = {
        windows = {
            ["latest"] = { ref = "0.4.40" },
            ["0.4.40"] = {
                url = "https://gitee.com/sunrisepeak/xlings-pkg/releases/download/mdbook/mdbook-v0.4.40-x86_64-pc-windows-msvc.zip",
                sha256 = nil
            }
        },
        debain = {
            ["latest"] = { ref = "0.4.43" },
            ["0.4.43"] = {
                url = "https://github.com/rust-lang/mdBook/releases/download/v0.4.43/mdbook-v0.4.43-x86_64-unknown-linux-gnu.tar.gz",
                sha256 = "d20c2f20eb1c117dc5ebeec120e2d2f6455c90fe8b4f21b7466625d8b67b9e60"
            },
            ["0.4.40"] = {
                url = "https://github.com/rust-lang/mdBook/releases/download/v0.4.40/mdbook-v0.4.40-x86_64-unknown-linux-gnu.tar.gz",
                sha256 = "9ef07fd288ba58ff3b99d1c94e6d414d431c9a61fdb20348e5beb74b823d546b"
            },
        },
        archlinux = { ref = "debain" },
        ubuntu = { ref = "debain" },
    },
}

import("platform")

local bindir = platform.get_config_info().bindir

local mdbook_file = {
    windows = "mdbook.exe",
    linux = "mdbook",
}

function installed()
    os.exec("mdbook --version")
    return true
end

function install()
    os.cp(mdbook_file[os.host()], bindir)
    return true
end

function uninstall()
    os.tryrm(path.join(bindir, mdbook_file[os.host()]))
    return true
end