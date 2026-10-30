package("lattice")
    set_description("A C++23 component runtime and tooling library")
    set_license("GPL-3.0-only")

    set_sourcedir(path.join(os.scriptdir(), "../../.."))

    add_configs("shared", {
        description = "Build the shared library",
        default = true,
        type = "boolean",
        readonly = true
    })

    add_deps("glm 1.0.1", "lattice-logger", "lattice-sysinfo")

    add_links("Lattice")
    if is_plat("linux") then
        add_syslinks("dl", "pthread")
    end

    on_install(function (package)
        local source_root = os.curdir()
        local build_root = path.join(package:builddir(), "source", "Lattice")

        os.tryrm(path.directory(build_root))
        os.mkdir(build_root)
        os.cp(path.join(source_root, "xmake.lua"), build_root)
        -- Dependency versions are resolved by the package recipe; do not clone the
        -- standalone project's locked repository for every consumer build.
        os.cp(path.join(source_root, "Lattice.hpp"), build_root)
        os.cp(path.join(source_root, "Composition.hpp"), build_root)
        os.cp(path.join(source_root, "kernel"), build_root)
        local stubs = import("stubs", {rootdir = path.join(package:scriptdir(), "../..")})
        io.writefile(path.join(build_root, "packaged.lua"), stubs(package, {"lattice-logger", "lattice-sysinfo"}))

        os.cd(build_root)
        import("package.tools.xmake").install(package, {
            lattice_build_extras = false,
            lattice_external_tools = true
        })
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            void test() {
                Lattice::Blueprints blueprints;
                (void)blueprints.add("Package::Consumer");
            }
        ]]}, {
            configs = {languages = "c++23"},
            includes = "Lattice/Lattice.hpp"
        }))
    end)
