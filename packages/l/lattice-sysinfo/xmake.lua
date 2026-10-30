package("lattice-sysinfo")
    set_description("Reusable SysInfo library without the Lattice runtime")
    set_license("GPL-3.0-only")
    set_sourcedir(path.join(os.scriptdir(), "../../../libs/lattice-sysinfo"))
    add_deps("lattice-logger")
    add_configs("shared", {description = "Build a shared library", default = true, type = "boolean", readonly = true})
    add_links("lattice-sysinfo")

    -- Consumers must also run directly, outside xmake's injected library path.
    on_load(function (package)
        if not package:is_plat("windows", "mingw") then
            package:add("rpathdirs", path.join(package:installdir(), "lib"))
        end
    end)

    on_install(function (package)
        local build_root = path.join(package:builddir(), "source", "lattice-sysinfo")
        os.tryrm(build_root)
        os.mkdir(build_root)
        os.cp("*", build_root)
        local script = io.readfile(path.join(build_root, "xmake.lua"))
        local stubs = import("stubs", {rootdir = path.join(package:scriptdir(), "../..")})
        io.writefile(path.join(build_root, "xmake.lua"),
            'set_languages("c++23")\nadd_rules("mode.debug", "mode.release")\n' .. stubs(package, {"lattice-logger"}) .. script)
        os.cd(build_root)
        import("package.tools.xmake").install(package, {}, {target = "lattice-sysinfo"})
    end)
    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            void test() { (void)Lattice::SystemInfo::collectBuild(); }
        ]]}, {configs = {languages = "c++23"}, includes = "lattice-sysinfo/SystemInfo.hpp"}))
    end)
