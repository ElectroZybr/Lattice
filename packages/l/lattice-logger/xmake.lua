package("lattice-logger")
    set_description("Reusable Logger library without the Lattice runtime")
    set_license("GPL-3.0-only")
    set_sourcedir(path.join(os.scriptdir(), "../../../Libs/Logger"))
    set_policy("package.install_always", true)
    add_deps("lattice-tools")
    add_configs("shared", {description = "Build a shared library", default = true, type = "boolean", readonly = true})
    add_links("Logger")

    -- Consumers must also run directly, outside xmake's injected library path.
    on_load(function (package)
        if not package:is_plat("windows", "mingw") then
            package:add("rpathdirs", path.join(package:installdir(), "lib"))
        end
    end)

    on_install(function (package)
        local build_root = path.join(package:builddir(), "source", "Logger")
        os.tryrm(build_root)
        os.mkdir(build_root)
        os.cp("*", build_root)
        local script = io.readfile(path.join(build_root, "xmake.lua"))
        script = script:gsub('add_deps%(.-, {public = true}%)', 'add_packages("lattice-tools", {public = true})')
        io.writefile(path.join(build_root, "xmake.lua"),
            'set_languages("c++23")\nadd_rules("mode.debug", "mode.release")\nadd_requires("lattice-tools")\n' .. script)
        os.cd(build_root)
        import("package.tools.xmake").install(package, {}, {target = "Logger"})
    end)
    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            void test() { Logger::info("consumer", "standalone logger"); }
        ]]}, {configs = {languages = "c++23"}, includes = "Logger/Logger.hpp"}))
    end)
