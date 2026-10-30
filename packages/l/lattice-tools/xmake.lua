package("lattice-tools")
    set_kind("library", {headeronly = true})
    set_description("Header-only data and formatting utilities")
    set_license("GPL-3.0-only")
    set_sourcedir(path.join(os.scriptdir(), "../../../libs/lattice-tools"))
    add_deps("glm 1.0.1")

    on_install(function (package)
        local build_root = path.join(package:builddir(), "source", "Tools")
        os.tryrm(build_root)
        os.mkdir(build_root)
        os.cp("*", build_root)
        local script = io.readfile(path.join(build_root, "xmake.lua"))
        io.writefile(path.join(build_root, "xmake.lua"),
            'set_languages("c++23")\nadd_rules("mode.debug", "mode.release")\nadd_requires("glm 1.0.1")\n' .. script)
        os.cd(build_root)
        import("package.tools.xmake").install(package, {}, {target = "lattice-tools"})
    end)
    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            void test() { Lattice::Value value{42}; (void)value; }
        ]]}, {configs = {languages = "c++23"}, includes = "lattice-tools/Value.hpp"}))
    end)
