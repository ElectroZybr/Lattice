-- History of benchmark runs: results repository (git), comparison and statistics.
-- No Lattice runtime: only BmRunner (result format), SysInfo and Logger.
target("lbm-history")
    set_kind("static")
    if not is_plat("windows", "mingw") then
        add_cxflags("-fPIC")
    end
    add_files("lbm-history/*.cpp")
    add_headerfiles("(lbm-history/*.hpp)")
    add_includedirs(".", {public = true})
    add_deps("lbm-runner", {public = true})

-- Tests of the library without the runtime: xmake run lbm-history.tests
if has_config("lattice_build_extras") then
    target("lbm-history.tests")
        set_kind("binary")
        add_files("tests/*_tests.cpp", "../tests/main.cpp")
        add_deps("lbm-history", "lattice-test")
end
