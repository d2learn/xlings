import("xim.pm.wrapper.pacman")

function installed(git_url)
    return pacman.installed(parse_name_from_git_url(git_url))
end

function deps(git_url)
    return pacman.deps(parse_name_from_git_url(git_url))
end

function install(git_url)
    local name = parse_name_from_git_url(git_url)

    -- 克隆 AUR 仓库
    if not clone_aur_package(name, git_url) then
        cprint(string.format("Failed to clone AUR repository: '%s'", git_url))
    end

    -- 进入包目录
    local ok = os.chdir(name)
    if not ok then
        cprint(string.format("Failed to enter directory: '%s'", name))
    end

    -- 构建并安装包
    local installed_ok = build_and_install(name)
    if not installed_ok then
        cprint(string.format("Failed to build and install package: '%s'", name))
    end

    return true
end

function uninstall(git_url)
    return pacman.uninstall(parse_name_from_git_url(git_url))
end

function info(git_url)
    return pacman.info(parse_name_from_git_url(git_url))
end

function parse_name_from_git_url(git_url)
    -- 假设输入形如 "https://aur.archlinux.org/<name>.git"
    local name = git_url:match("^https://aur%.archlinux%.org/(.-)%.git$")
    if not name then
        cprint(string.format("Failed to parse package name from git URL: '%s'", git_url))
    end
    return name
end

function clone_aur_package(name, git_url)
    local ok = os.execv("git", {"clone", git_url, name})
    return ok == 0
end

function build_and_install(name)
    local ok = os.execv("makepkg", {"-si"})
    return ok == 0
end

function main()
    local aur_git_url = "https://aur.archlinux.org/yay.git"

    -- 检查是否已安装
    cprint(installed(aur_git_url)) -- true or false

    -- 获取依赖
    cprint(deps(aur_git_url))

    -- 安装 AUR 包
    aur_install(aur_git_url)

    -- 获取包信息
    cprint(info(aur_git_url))
end