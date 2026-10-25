target("lattice-sysinfo")
    set_kind("shared")
    set_version("0.1.0", {soname = true})
    set_symbols("hidden")
    add_defines("SYSINFO_BUILDING_LIBRARY")
    add_files("lattice-sysinfo/**.cpp")
    add_headerfiles("(lattice-sysinfo/**.hpp)")
    add_includedirs(".", {public = true})
    add_deps("lattice-logger", {public = true})
    if is_plat("linux") then
        add_syslinks("pthread", {public = true})
    end

-- Tests of the library without the runtime: xmake run lattice-sysinfo.tests
if has_config("lattice_build_extras") then
    target("lattice-sysinfo.tests")
        set_kind("binary")
        add_files("tests/*_tests.cpp", "../tests/main.cpp", "../../tools/Tests.cpp")
        add_defines("LATTICE_BUILDING_LIBRARY")
        add_includedirs("../../..")
        add_deps("lattice-sysinfo")
end
