import("common")
import("xim.base.utils")

local XPackage = {}
XPackage.__index = XPackage

local os_info = utils.os_info()

function new(pkg)
    local instance = {}
    debug.setmetatable(instance, XPackage)
    instance.version, instance.pdata = _dereference(pkg.version, pkg.data.package)
    instance._map_pkgname = nil -- for pm wrapper
    instance.hooks = {
        installed = pkg.data.installed,
        build = pkg.data.build,
        install = pkg.data.install,
        config = pkg.data.config,
        uninstall = pkg.data.uninstall,
    }
    return instance
end

function XPackage:info()
    return {
        name = self.pdata.name,
        homapage = self.pdata.homapage,
        version = self.version,
        authors = self.pdata.authors,
        maintainers = self.pdata.maintainers,
        contributors = self.pdata.contributors,
        license = self.pdata.license,
        repo = self.pdata.repo,
        docs = self.pdata.docs,
        description = self.pdata.description,
    }
end

function XPackage:name()
    return self.pdata.name
end

function XPackage:xpm_enable()
    local xpm = self.pdata.xpm
    if not xpm or not xpm[os_info.name] then
        return false
    end
    return xpm[os_info.name][self.version] ~= nil
end

function XPackage:get_xpm_resources()
    return self.pdata.xpm[os_info.name][self.version]
end

function XPackage:deps()
    if not self.pdata.xpm or not self.pdata.xpm[os_info.name] then
        return {}
    end
    return self.pdata.xpm[os_info.name].deps or {}
end

function XPackage:get_pm_wrapper()
    -- self.version is package manager name when use local package manager
    return self.version
end

function XPackage:get_map_pkgname()
    return self.pdata.pm_wrapper[self.version]
end

function _dereference(version, package)
    if package.xpm then
        if package.xpm[os_info.name] then
            local ref = package.xpm[os_info.name].ref
            if ref then
                package.xpm[os_info.name] = package.xpm[ref]
            end
            ref = package.xpm[os_info.name][version].ref
            if ref then
                package.xpm[os_info.name][version] = package.xpm[os_info.name][ref]
                version = ref
            end
        end
    end
    return version, package
end

--- XPackage Spec

--[[

package = {
    -- base info
    homepage = "https://example.com",

    name = "package-name",
    description = "Package description",

    authors = "Author Name",
    maintainers = "Maintainer Name",
    contributors = "Contributor Name",
    license = "MIT",
    repo = "https://example.com/repo",
    docs = "https://example.com/docs",

    -- xim pkg info
    archs = {"x86_64"},
    status = "stable", -- dev, stable, deprecated
    categories = {"category1", "category2"},
    keywords = {"keyword1", "keyword2"},
    date = "2020-01-01",

    -- env info - todo
    xvm_type = "", -- unused
    xvm_support = false, -- unused
    xvm_default = false,

    xpm = {
        windows = {
            deps = {"dep1", "dep2"},
            -- TODO: support url pattern
            --["url_template"] = "https://example.com/version.zip",
            --["1.0.0"] = "url_template", -- https://example.com/1.0.0.zip
            ["1.0.1"] = {"url", "sha256"},
            ["1.0.0"] = {"url", "sha256"},
        },
        ubuntu = {
            deps = {"dep3", "dep4"},
            ["1.0.1"] = {"url", "sha256"},
            ["1.0.0"] = {"url", "sha256"},
        },
    },

    pm_wrapper = {
        apt = "xxxx",
        winget = "xxxx",
        pacman = "xxxx",
    }
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