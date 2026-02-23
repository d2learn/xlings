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
        local llvm_prefix = os.getenv("LLVM_PREFIX") or "/opt/homebrew/opt/llvm@20"
        local libcxx_dir = llvm_prefix .. "/lib/c++"
        -- Static link libc++ so binary has no runtime dependency on LLVM toolchain
        add_ldflags("-nostdlib++", {force = true})
        add_ldflags(libcxx_dir .. "/libc++.a", {force = true})
        add_ldflags(libcxx_dir .. "/libc++experimental.a", {force = true})
        add_ldflags("-lc++abi", {force = true})
    elseif is_plat("linux") then
        -- Use system dynamic linker (glibc) so binary is not tied to SDK path (e.g. /home/xlings/.xlings_data/...)
        add_ldflags("-Wl,-dynamic-linker,/lib64/ld-linux-x86-64.so.2", {force = true})
        -- Static link stdc++/gcc for release so binary does not depend on SDK libs
        if not os.getenv("XLINGS_NOLINKSTATIC") then
            add_ldflags("-static-libstdc++", "-static-libgcc", {force = true})
        end
    end