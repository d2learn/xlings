add_rules("mode.debug", "mode.release")

set_languages("c++23")

-- C++23 main binary (json: self-contained in core/json/, like d2x)
target("xlings")
    set_kind("binary")
    add_files("core/main.cpp")
    add_files("core/**.cppm")
    add_includedirs("core/json")
    set_policy("build.c++.modules", true)

    if is_plat("macosx") then
        -- brew install llvm@20
        -- macOS: use dynamic linking to avoid ABI conflicts with dependencies
        -- Dependencies (ftxui, llmapi) are built with system libc++
        -- Static linking causes memory allocator conflicts
        add_linkdirs("/opt/homebrew/Cellar/llvm@20/20.1.8/lib/c++")
        add_ldflags("-lc++experimental", {force = true})
        -- Add rpath to find homebrew libc++ at runtime
        add_ldflags("-Wl,-rpath,/opt/homebrew/opt/llvm@20/lib/c++", {force = true})
    elseif is_plat("linux") then
        -- Use system dynamic linker (glibc) so binary is not tied to SDK path (e.g. /home/xlings/.xlings_data/...)
        add_ldflags("-Wl,-dynamic-linker,/lib64/ld-linux-x86-64.so.2", {force = true})
        -- Static link stdc++/gcc for release so binary does not depend on SDK libs
        if not os.getenv("XLINGS_NOLINKSTATIC") then
            add_ldflags("-static-libstdc++", "-static-libgcc", {force = true})
        end
    end