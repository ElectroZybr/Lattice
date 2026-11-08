target("lattice-sysinfo")
    set_kind("shared")
    set_version("1.0.0", {soname = true})
    set_symbols("hidden")
    add_defines("SYSINFO_BUILDING_LIBRARY")
    add_files("lattice-sysinfo/**.cpp")
    add_headerfiles("(lattice-sysinfo/*.hpp)")
    add_includedirs(".", {public = true})
    add_deps("lattice-logger", {public = true})
    -- Finds its shared deps beside itself: Ubuntu links with --as-needed, so a binary may not list them.
    add_rpathdirs("@loader_path")
    if is_plat("linux") then
        add_syslinks("pthread", {public = true})
    elseif is_plat("windows", "mingw") then
        add_syslinks("advapi32", "dxgi", "powrprof", "setupapi", "cfgmgr32", "iphlpapi", "ws2_32")
    elseif is_plat("macosx") then
        add_frameworks("IOKit", "CoreFoundation")
    end