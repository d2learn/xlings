add_rules("mode.debug", "mode.release")

set_languages("c++23")

add_repositories("mcpplibs-index https://github.com/mcpplibs/mcpplibs-index.git")

add_requires("cmdline 0.0.2")
add_requires("ftxui 6.1.9")
add_requires("mcpplibs-capi-lua")
add_requires("mcpplibs-xpkg 0.0.6")
add_requires("gtest 1.15.2")

-- C++23 main binary
target("xlings")
    set_kind("binary")
    add_files("core/main.cpp")
    add_files("core/**.cppm")
    add_includedirs("core/json")
    add_packages("cmdline", "ftxui", "mcpplibs-capi-lua", "mcpplibs-xpkg")
    set_policy("build.c++.modules", true)

    if is_plat("macosx") then
        set_toolchains("llvm")
    elseif is_plat("linux") then
        if not os.getenv("XLINGS_NOLINKSTATIC") then
            add_ldflags("-static", {force = true})
        end
        local gcc_sdk = os.getenv("GCC_SDK")
        if gcc_sdk and #gcc_sdk > 0 then
            add_linkdirs(gcc_sdk .. "/lib64", {force = true})
        end
    end

-- Unit tests
target("xlings_tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/unit/*.cpp")
    add_files("core/**.cppm")
    add_includedirs("core/json")
    add_packages("cmdline", "ftxui", "mcpplibs-capi-lua", "mcpplibs-xpkg", "gtest")
    set_policy("build.c++.modules", true)

    if is_plat("macosx") then
        set_toolchains("llvm")
    elseif is_plat("linux") then
        if not os.getenv("XLINGS_NOLINKSTATIC") then
            add_ldflags("-static", {force = true})
        end
        local gcc_sdk = os.getenv("GCC_SDK")
        if gcc_sdk and #gcc_sdk > 0 then
            add_linkdirs(gcc_sdk .. "/lib64", {force = true})
        end
    end
