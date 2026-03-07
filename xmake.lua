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
        local llvm_prefix = os.getenv("LLVM_PREFIX")
        if llvm_prefix then -- if LLVM_PREFIX is set, we assume it's a recent version of LLVM that provides its own libc++ with C++23 support
            -- 静态链接 LLVM 自带的 libc++，避免依赖系统 libc++.dylib 中缺失的 C++23 符号
            -- (std::println 等 C++23 特性需要 macOS 15+ 的 libc++，静态链接后可在 macOS 11+ 运行)
            add_ldflags("-nostdlib++", {force = true})
            add_ldflags(llvm_prefix .. "/lib/libc++.a", {force = true})
            add_ldflags(llvm_prefix .. "/lib/libc++abi.a", {force = true})
        end
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
        local llvm_prefix = os.getenv("LLVM_PREFIX")
        if llvm_prefix then -- if LLVM_PREFIX is set, we assume it's a recent version of LLVM that provides its own libc++ with C++23 support
            -- 静态链接 LLVM 自带的 libc++，避免依赖系统 libc++.dylib 中缺失的 C++23 符号
            -- (std::println 等 C++23 特性需要 macOS 15+ 的 libc++，静态链接后可在 macOS 11+ 运行)
            add_ldflags("-nostdlib++", {force = true})
            add_ldflags(llvm_prefix .. "/lib/libc++.a", {force = true})
            add_ldflags(llvm_prefix .. "/lib/libc++abi.a", {force = true})
        end
    elseif is_plat("linux") then
        add_ldflags("-static", {force = true})
    end
