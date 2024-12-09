import("common")
import("xim.base.utils")

local XPackage = {}
XPackage.__index = XPackage

local os_type = utils.os_type()

function new(pdata)
    local instance = {}
    debug.setmetatable(instance, XPackage)
    instance.pdata = pdata.package
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
    return self.pdata.support[os_type]
end

function XPackage:source()
    local pms = self:get_pmanager()
    if not pms then
        return nil
    end
    return pms.xpm
end

function XPackage:deps()
    if not self.pdata.deps then
        return {}
    end
    return self.pdata.deps[os_type]
end

function XPackage:get_pmanager()
    if not self.pdata.pmanager then
        return nil -- if pmanager is nil, default use xim
    end
    return self.pdata.pmanager[os_type]
end

--- XPackage Spec

--[[

package = {
    -- base info
    homepage = "https://example.com",

    name = "package-name",
    version = "1.0.0",
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
    env_type = "", -- unused

    -- TODO: arch, os version support
    support = {
        windows = true,
        ubuntu = true,
        arch = true,
    },

    deps = {
        windows = { "xpkgname1", "xpkgname2" },
        ubuntu = { "xpkgname3", "xpkgname2@1.0.1" },
        arch = { "xpkgname4", "xpkgname2" },
    },

    -- if pmanager is nil, default use xim
    pmanager = {
        -- if defined, need clearly define the package manager
        windows = {
            xpm = {url = "https://example.com/xim-installer.exe", sha256 = "hash"},
        },
        ubuntu = {
            apt = "package-name",
            snap = "package-name",
        },
        arch = {
            pacman = "package-name",
            aur = "package-name",
        },
    },

    -- TODO: install path
}

-- xim: hooks for package manager

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