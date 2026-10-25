target("lattice-logger")
    set_kind("shared")
    set_version("0.1.0", {soname = true})
    set_symbols("hidden")
    add_defines("LOGGER_BUILDING_LIBRARY")
    add_files("lattice-logger/**.cpp")
    add_headerfiles("(lattice-logger/**.hpp)")
    add_includedirs(".", {public = true})
    add_deps("lattice-tools", {public = true})
    if is_plat("linux") then
        add_syslinks("pthread", {public = true})
    end

-- Tests of the library without the runtime: xmake run lattice-logger.tests
if has_config("lattice_build_extras") then
    target("lattice-logger.tests")
        set_kind("binary")
        add_files("tests/*_tests.cpp", "../tests/main.cpp", "../../tools/Tests.cpp")
        add_defines("LATTICE_BUILDING_LIBRARY")
        add_includedirs("../../..")
        add_deps("lattice-logger")
end
