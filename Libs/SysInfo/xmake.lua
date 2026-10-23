target("SysInfo")
    set_kind("shared")
    set_version("0.1.0", {soname = true})
    set_symbols("hidden")
    add_defines("SYSINFO_BUILDING_LIBRARY")
    add_files("SysInfo/**.cpp")
    add_headerfiles("(SysInfo/**.hpp)")
    add_includedirs(".", {public = true})
    add_deps("Logger", {public = true})
    if is_plat("linux") then
        add_syslinks("pthread", {public = true})
    end
