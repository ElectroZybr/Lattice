-- Registry, assertions and results: no console, no runtime.
target("lattice-test")
    set_kind("shared")
    set_version("0.1.0", {soname = true})
    set_symbols("hidden")
    add_defines("LATTICE_TEST_BUILDING_LIBRARY")
    add_files("lattice-test/*.cpp")
    add_headerfiles("(lattice-test/*.hpp)")
    add_includedirs(".", {public = true})
    add_deps("lattice-tools", {public = true})

-- The standard main of a test executable; a test target depends on this one.
target("lattice-test-main")
    set_kind("static")
    add_files("main/Main.cpp")
    add_deps("lattice-test", {public = true})
    add_deps("lattice-logger", {public = true})

if has_config("lattice_build_extras") then
    target("lattice-test.tests")
        set_kind("binary")
        add_files("tests/*.cpp")
        add_deps("lattice-test-main")
end
