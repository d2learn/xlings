import("common")
import("xim.base.utils")

local XPackage = {}
XPackage.__index = XPackage

local os_info = utils.os_info()

function new(pkg)
    local instance = {}
    debug.setmetatable(instance, XPackage)

    -- init system info
    instance.__path = pkg.path

    -- init package data
    instance:_init(pkg.version, pkg.data.package)
    
    -- init hooks
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
        type = self.pdata.type,
        name = self.pdata.name,
        homepage = self.pdata.homepage,
        version = self.version,
        authors = self.pdata.authors,
        maintainers = self.pdata.maintainers,
        categories = self.pdata.categories,
        keywords = self.pdata.keywords,
        contributors = self.pdata.contributors,
        licenses = self.pdata.licenses or self.pdata.license,
        repo = self.pdata.repo,
        docs = self.pdata.docs,
        forum = self.pdata.forum,
        description = self.pdata.description,
        programs = self.pdata.programs,
    }
end

function XPackage:has_xpm()
    local xpm = self.pdata.xpm
    if not xpm or not xpm[os_info.name] then
        return false
    end
    return xpm[os_info.name][self.version] ~= nil
end

function XPackage:get_xpm_resources()
    return self.pdata.xpm[os_info.name][self.version]
end

function XPackage:get_deps()
    if not self.pdata.xpm or not self.pdata.xpm[os_info.name] then
        return {}
    end
    return self.pdata.xpm[os_info.name].deps or {}
end

-- _derefence for data
function XPackage:_init(version, package)
    local real_os_key = utils.xpm_target_os_helper(package.xpm)
    local _, entry = utils.deref(package.xpm, real_os_key)
    -- if xpm[os_info.name] is not found or is ref, add/update it
    package.xpm[os_info.name] = entry
    version, _ = utils.deref(package.xpm[os_info.name], version)

    -- x-package data
    self.name = package.name
    self.version = version
    if package.namespace then
        self.namespace = package.namespace
    end
    self._real_os_key = real_os_key
    self.type = package.type or "package"

    self.pdata = package

    --return version, package
end

--- XPackage Spec

--[[

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
}

-- xim: hooks for package manager

import("xim.base.runtime")

-- pkginfo = runtime.get_pkginfo()
-- pkginfo = {install_file = "", install_dir = "", version = "x.x.x"}

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