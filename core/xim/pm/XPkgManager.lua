import("common")
-- xim package template

local XPackage = {}
XPackage.__index = XPackage

-- package: xim package spec: todo
function new(pkg)
    local instance = {}
    debug.setmetatable(instance, XPackage)
    instance.package = pkg.package
    instance.hooks = {
        support = pkg.support or nil,
        installed = pkg.installed or nil,
        deps = pkg.deps or nil,
        install = pkg.install or nil,
        config = pkg.config or nil,
        uninstall = pkg.uninstall or nil,
        info = pkg.info or nil,
    }
    return instance
end

function XPackage:support()
    return _attr_template(self, "support")
end

function XPackage:installed()
    return _cmds_template(self, "installed")
end

function XPackage:deps()
    return _attr_template(self, "deps")
end

function XPackage:install()
    return _cmds_template(self, "install")
end

function XPackage:config()
    return _cmds_template(self, "config")
end

function XPackage:uninstall()
    return _cmds_template(self, "uninstall")
end

function XPackage:info()
    return try {
        function()
            local info = {}
            if self.hooks.info then
                info = self.hooks.info()
            else
                info = {
                    name = self.package.name,
                    homapage = self.package.homapage,
                    author = self.package.author,
                    maintainers = self.package.maintainers,
                    license = self.package.license,
                    github = self.package.github,
                    docs = self.package.docs,
                }
            end
            cprint("\n--- ${cyan}info${clear}")

            local fields = {
                {key = "name", label = "name"},
                {key = "homepage", label = "homepage"},
                {key = "author", label = "author"},
                {key = "maintainers", label = "maintainers"},
                {key = "licenses", label = "licenses"},
                {key = "github", label = "github"},
                {key = "docs", label = "docs"},
                --{key = "profile", label = "profile"}
            }
        
            cprint("")
            for _, field in ipairs(fields) do
                local value = info[field.key]
                if value then
                    cprint(string.format("${bright}%s:${clear} ${dim}%s${clear}", field.label, value))
                end
            end

            cprint("")

            if info["profile"] then
                cprint( "\t${green bright}" .. info["profile"] .. "${clear}")
            end

            cprint("")
        end, catch {
            function(e)
                cprint("[xlings:xim]: ${yellow}warning:${clear} %s", e)
                print(self)
                cprint("[xlings:xim]: ${yellow}please check package file - info${clear}")
                return false
            end
        }
    }
end

---

function _cmds_template(xpm, attr)
    return try {
        function()
            if xpm.hooks[attr] then
                return xpm.hooks[attr]()
            else
                local cmds = xpm.package[attr]
                for _, cmd in ipairs(cmds) do
                    if type(cmd) == "table" then
                        local os_name = common.get_linux_distribution().name
        
                        if os.host() == "windows" or os.host() == "macosx" then
                            os_name = os.host()
                        end
        
                        if os_name == cmd[1] then
                            common.xlings_exec(cmd[2])
                        else
                            -- TODO: support other platform
                            print("skip cmd: " .. cmd[2] .. " - " .. cmd[1])
                        end
                    else
                        common.xlings_exec(cmd)
                    end
                end
            end
            return true
        end, catch {
            function(e)
                cprint("[xlings:xim]: ${yellow}warning:${clear} %s", e)
                print(xpm)
                cprint("[xlings:xim]: ${yellow}please check package file - %s${clear}", attr)
                return false
            end
        }
    }
end

function _attr_template(xpm, attr)
    return try {
        function()
            if xpm.hooks[attr] then
                return xpm.hooks[attr]()[os.host()]
            else
                return xpm.package[attr][os.host()]
            end
            return true
        end, catch {
            function(e)
                print(xpm)
                cprint("[xlings:xim]: ${yellow}warning:${clear} %s", e)
                cprint("[xlings:xim]: ${yellow}please checker package file${clear} %s", e)
                return false
            end
        }
    }
end