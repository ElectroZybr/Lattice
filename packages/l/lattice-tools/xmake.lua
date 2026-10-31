package("lattice-tools")
    set_kind("library", {headeronly = true})
    set_description("Header-only data and formatting utilities")
    set_license("GPL-3.0-only")
    on_source(function (package)
        import("sources", {rootdir = path.join(package:scriptdir(), "../..")})(package)
    end)
    add_deps("glm 1.0.1")

    on_install(function (package)
        local build_root = path.join(package:cachedir(), "staged", "Tools")
        os.tryrm(build_root)
        os.mkdir(build_root)
        os.cp("libs/lattice-tools/*", build_root)
        local script = io.readfile(path.join(build_root, "xmake.lua"))
        io.writefile(path.join(build_root, "xmake.lua"),
            'set_languages("c++23")\nadd_rules("mode.debug", "mode.release")\nadd_requires("glm 1.0.1")\n' .. script)
        -- Without its own xmake.lua the nested xmake would climb to a parent project and hang there.
        assert(os.isfile(path.join(build_root, "xmake.lua")), "no sources staged in " .. build_root)
        os.cd(build_root)
        import("package.tools.xmake").install(package, {}, {target = "lattice-tools"})
    end)
    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            void test() { Lattice::Value value{42}; (void)value; }
        ]]}, {configs = {languages = "c++23"}, includes = "lattice-tools/Value.hpp"}))
    end)
