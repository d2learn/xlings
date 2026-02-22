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
        add_ldflags("-lc++experimental", {force = true})
    elseif is_plat("linux") then
        -- Use system dynamic linker (glibc) so binary is not tied to SDK path (e.g. /home/xlings/.xlings_data/...)
        add_ldflags("-Wl,-dynamic-linker,/lib64/ld-linux-x86-64.so.2", {force = true})
        -- Static link stdc++/gcc for release so binary does not depend on SDK libs
        if not os.getenv("XLINGS_NOLINKSTATIC") then
            add_ldflags("-static-libstdc++", "-static-libgcc", {force = true})
        end
    end