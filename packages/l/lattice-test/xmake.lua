package("lattice-test")
    set_description("Unit tests and fixtures without the Lattice runtime")
    set_license("GPL-3.0-only")
    set_sourcedir(path.join(os.scriptdir(), "../../../libs/lattice-test"))
    add_deps("lattice-logger")
    add_configs("shared", {description = "Build a shared library", default = true, type = "boolean", readonly = true})
    add_links("lattice-test")

    -- Consumers must also run directly, outside xmake's injected library path.
    on_load(function (package)
        if not package:is_plat("windows", "mingw") then
            package:add("rpathdirs", path.join(package:installdir(), "lib"))
        end
    end)

    on_install(function (package)
        local build_root = path.join(package:builddir(), "source", "lattice-test")
        os.tryrm(build_root)
        os.mkdir(build_root)
        os.cp("*", build_root)
        local script = io.readfile(path.join(build_root, "xmake.lua"))
        script = script:gsub('add_deps%(.-, {public = true}%)', 'add_packages("lattice-logger", {public = true})')
        io.writefile(path.join(build_root, "xmake.lua"),
            'set_languages("c++23")\nadd_rules("mode.debug", "mode.release")\nadd_requires("lattice-logger")\n' .. script)
        os.cd(build_root)
        import("package.tools.xmake").install(package, {}, {target = "lattice-test"})
    end)
    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            TEST(Standalone, Lattice::Fixture) { REQUIRE(true); CHECK(true, "message"); }
            int main() { return Lattice::Test::instance().runAll(); }
        ]]}, {configs = {languages = "c++23"}, includes = "lattice-test/Tests.hpp"}))
    end)
