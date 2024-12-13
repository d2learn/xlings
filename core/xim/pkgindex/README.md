# XIM Package Index Repository

## XPackage Spec

```lua
package = {
    -- base info
    homepage = "https://example.com",

    name = "package-name",
    description = "Package description",

    authors = "Author Name",
    maintainers = "Maintainer Name or url",
    contributors = "Contributor Name or url",
    license = "MIT",
    repo = "https://example.com/repo",
    docs = "https://example.com/docs",

    -- xim pkg info
    archs = {"x86_64"},
    status = "stable", -- dev, stable, deprecated
    categories = {"category1", "category2"},
    keywords = {"keyword1", "keyword2"},
    date = "2024-12-01",

    -- env info - todo
    xvm_type = "", -- unused
    xvm_support = false, -- unused
    xvm_default = false,

    xpm = {
        windows = {
            deps = {"dep1", "dep2"},
            ["1.0.1"] = {"url", "sha256"},
            ["1.0.0"] = {"url", "sha256"},
        },
        ubuntu = {
            deps = {"dep3", "dep4"},
            ["latest"] = { ref = "1.0.1"},
            ["1.0.1"] = {"url", "sha256"},
            ["1.0.0"] = {"url", "sha256"},
        },
    },

    pm_wrapper = {
        pacman = "pakcage name",
    }
}

-- xim: hooks for package manager

import("xim.base.runtime")

-- pkginfo = runtime.get_pkginfo()
-- pkginfo = {install_file = "", version = "x.x.x"}

-- step 1: support check - package attribute

-- step 2: installed check
function installed()
    return true
end

-- step 2.5: download resources/package
-- step 3: process dependencies - package attribute

-- step 4: build package
function build()
    return true
end

-- step 5: install package
function install()
    return true
end

-- step 6: configure package
function config()
    return true
end

-- step 7: uninstall package
function uninstall()
    return true
end
```

## Examples

### java's package file - pm wrapper

```lua
package = {
    -- base info
    name = "java",

    -- xim pkg info
    status = "stable", -- dev, stable, deprecated
    categories = {"plang", "pm-wrapper"},
    keywords = {"java", "openjdk"},

    pm_wrapper = {
        winget = "AdoptOpenJDK.OpenJDK.8",
        apt = "openjdk-8-jdk",
        pacman = "jdk8-openjdk",
    }
}
```

### mdbook's pakcage file - xpm

```lua
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
                sha256 = nil
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
```