import("common")
import("xim.base.utils")

local XPackage = {}
XPackage.__index = XPackage

local os_info = utils.os_info()

function new(pdata)
    local instance = {}
    debug.setmetatable(instance, XPackage)
    instance.pdata = pdata.package
    instance.version = pdata.version
    instance.hooks = {
        installed = pdata.installed,
        build = pdata.build,
        install = pdata.install,
        config = pdata.config,
        uninstall = pdata.uninstall,
    }
    return instance
end

function XPackage:info()
    return {
        name = self.pdata.name,
        homapage = self.pdata.homapage,
        version = self.pdata.version,
        author = self.pdata.author,
        maintainer = self.pdata.maintainer,
        contributor = self.pdata.contributor,
        license = self.pdata.license,
        repo = self.pdata.repo,
        docs = self.pdata.docs,
        description = self.pdata.description,
    }
end

function XPackage:name()
    return self.pdata.name
end

function XPackage:support()
    local pm = self.pdata.pmanager
    if pm[self.pdata.version][os_info.name] then
        return true
    end
    return false
end

function XPackage:get_xpm()
    return pms.xpm
end

function XPackage:deps()
    if not self.pdata.deps then
        return nil
    end
    return self.pdata.deps[os_type]
end

function XPackage:get_pmanager()
    local pm = self.pdata.pmanager
    if not pm then
        cprint("[xlings:xim]: get_pmanager: package manager not found")
        return { xpm = { url = nil, sha256 = nil } }
    end
    return pm[self.pdata.version][os_info.name]
end

--- XPackage Spec

--[[

package = {
    -- base info
    homepage = "https://example.com",

    name = "package-name",
    version = "1.x.x",
    description = "Package description",

    author = "Author Name",
    maintainer = "Maintainer Name",
    contributor = "Contributor Name",
    license = "MIT",
    repo = "https://example.com/repo",
    docs = "https://example.com/docs",

    -- xim pkg info
    status = "stable", -- dev, stable, deprecated
    categories = {"category1", "category2"},
    keywords = {"keyword1", "keyword2"},
    date = "2020-01-01",

    -- env info - todo
    xvm_type = "", -- unused
    xvm_support = false, -- unused
    xvm_default = false,

    deps = {
        windows = { "xpkgname1", "xpkgname2" },
        ubuntu = { "xpkgname3", "xpkgname2@1.0.1" },
        arch = { "xpkgname4", "xpkgname2" },
    },

    pmanager = {
        ["1.0.0"] = {
            windows = {
                xpm = { url = "https://example.com/package-1.0.0.exe", sha256 = "xxxx" }
            },
            ubuntu = { apt = "apt-package-name" },
            arch = { pacman = "pacman-package-name" },
        },
        ["1.0.1"] = {
            windows = {
                xpm = { url = "https://example.com/package-1.0.1.exe", sha256 = "xxxx" }
            },
            ubuntu = { apt = "apt-package-name"},
            arch = { pacman = "pacman-package-name"},
        },
    },

    -- TODO: install path
}

-- xim: hooks for package manager

inherit("xim.base.runtime")
-- runtime = {install_file = "", version = "x.x.x"}

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

]]