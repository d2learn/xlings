package = {
    -- base info
    name = "mdbook",
    version = "0.4.40", -- default version
    description = "Create book from markdown files. Like Gitbook but implemented in Rust",

    authors = "Mathieu David, Michael-F-Bryan, Matt Ickstadt",
    contributor = "https://github.com/rust-lang/mdBook/graphs/contributors",
    license = "MPL-2.0",
    repo = "https://github.com/rust-lang/mdBook",
    docs = "https://rust-lang.github.io/mdBook",

    -- xim pkg info
    status = "stable", -- dev, stable, deprecated
    categories = {"book", "markdown"},
    keywords = {"book", "gitbook", "rustbook", "markdown"},

    pmanager = {
        ["0.4.40"] = {
            windows = {
                xpm = {
                    url = "https://gitee.com/sunrisepeak/xlings-pkg/releases/download/mdbook/mdbook-v0.4.40-x86_64-pc-windows-msvc.zip",
                    sha256 = nil
                }
            },
            ubuntu = {
                xpm = {
                    url = "https://github.com/rust-lang/mdBook/releases/download/v0.4.40/mdbook-v0.4.40-x86_64-unknown-linux-gnu.tar.gz",
                    sha256 = "9ef07fd288ba58ff3b99d1c94e6d414d431c9a61fdb20348e5beb74b823d546b"
                }
            },
        },
    }
}

import("platform")

local bindir = platform.get_config_info().bindir

function installed()
    os.exec("mdbook --version")
    return true
end

function install()
    os.cp("mdbook", bindir)
    return true
end

function uninstall()
    os.tryrm(path.join(bindir, "mdbook"))
end