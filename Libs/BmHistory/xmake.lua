-- History of benchmark runs: results repository (git), comparison and statistics.
-- No Lattice runtime: only BmRunner (result format), SysInfo and Logger.
target("BmHistory")
    set_kind("static")
    if not is_plat("windows", "mingw") then
        add_cxflags("-fPIC")
    end
    add_files("BmHistory/*.cpp")
    add_headerfiles("(BmHistory/*.hpp)")
    add_includedirs(".", {public = true})
    add_deps("BmRunner", {public = true})
