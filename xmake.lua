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
        -- Static link LLVM libc++ so binary has no runtime dependency on LLVM toolchain.
        add_ldflags("-nostdlib++", {force = true})
        add_ldflags(libcxx_dir .. "/libc++.a", {force = true})
        add_ldflags(libcxx_dir .. "/libc++experimental.a", {force = true})
        add_ldflags("-lc++abi", {force = true})
    elseif is_plat("linux") then
        -- Prefer fully static Linux release binaries to avoid glibc runtime constraints.
        if not os.getenv("XLINGS_NOLINKSTATIC") then
            add_ldflags("-static", {force = true})
        end
        -- Fallback search path: allow using gcc SDK static libs when needed.
        local gcc_sdk = os.getenv("GCC_SDK")
        if gcc_sdk and #gcc_sdk > 0 then
            add_linkdirs(gcc_sdk .. "/lib64", {force = true})
        end
    end