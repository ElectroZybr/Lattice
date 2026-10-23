target("BmRunner")
    set_kind("shared")
    set_version("0.1.0", {soname = true})
    set_symbols("hidden")
    add_defines("BMRUNNER_BUILDING_LIBRARY")
    add_files("BmRunner/**.cpp")
    add_headerfiles("(BmRunner/**.hpp)")
    add_includedirs(".", {public = true})
    add_deps("Logger", "SysInfo", {public = true})
    if is_plat("linux") then
        add_syslinks("pthread", {public = true})
    end
