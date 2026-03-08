add_rules("mode.debug", "mode.release")

set_languages("c++23")

add_repositories("mcpplibs-index https://github.com/mcpplibs/mcpplibs-index.git")

add_requires("cmdline 0.0.2")
add_requires("ftxui 6.1.9")
add_requires("mcpplibs-capi-lua")
add_requires("mcpplibs-xpkg 0.0.24")
add_requires("gtest 1.15.2")

-- C++23 main binary
target("xlings")
    set_kind("binary")
    add_files("core/main.cpp")
    add_files("core/**.cppm")
    add_includedirs("core/json")
    add_packages("cmdline", "ftxui", "mcpplibs-capi-lua")
    add_packages("mcpplibs-xpkg")
    set_policy("build.c++.modules", true)

    if is_plat("macosx") then
        set_toolchains("llvm")
    elseif is_plat("linux") then
        add_ldflags("-static", {force = true})
    end

-- Unit tests
target("xlings_tests")
    set_kind("binary")
    set_default(false)
    set_rundir("$(projectdir)")
    add_files("tests/unit/*.cpp")
    add_files("core/**.cppm")
    add_includedirs("core/json")
    add_packages("cmdline", "ftxui", "mcpplibs-capi-lua", "gtest")
    add_packages("mcpplibs-xpkg")
    set_policy("build.c++.modules", true)

    if is_plat("macosx") then
        set_toolchains("llvm")
    elseif is_plat("linux") then
        add_ldflags("-static", {force = true})
    end
