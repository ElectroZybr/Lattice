target("Logger")
    set_kind("shared")
    set_version("0.1.0", {soname = true})
    set_symbols("hidden")
    add_defines("LOGGER_BUILDING_LIBRARY")
    add_files("Logger/**.cpp")
    add_headerfiles("(Logger/**.hpp)")
    add_includedirs(".", {public = true})
    add_deps("Lattice.Tools", {public = true})
    if is_plat("linux") then
        add_syslinks("pthread", {public = true})
    end
