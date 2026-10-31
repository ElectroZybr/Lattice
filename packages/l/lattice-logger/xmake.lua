package("lattice-logger")
    set_description("Reusable Logger library without the Lattice runtime")
    set_license("GPL-3.0-only")
    on_source(function (package)
        import("sources", {rootdir = path.join(package:scriptdir(), "../..")})(package)
    end)
    add_deps("lattice-tools")
    add_configs("shared", {description = "Build a shared library", default = true, type = "boolean", readonly = true})
    add_links("lattice-logger")

    -- Consumers must also run directly, outside xmake's injected library path.
    on_load(function (package)
        if not package:is_plat("windows", "mingw") then
            package:add("rpathdirs", path.join(package:installdir(), "lib"))
        end
    end)

    on_install(function (package)
        local build_root = path.join(package:cachedir(), "staged", "lattice-logger")
        os.tryrm(build_root)
        os.mkdir(build_root)
        os.cp("libs/lattice-logger/*", build_root)
        local script = io.readfile(path.join(build_root, "xmake.lua"))
        local stubs = import("stubs", {rootdir = path.join(package:scriptdir(), "../..")})
        io.writefile(path.join(build_root, "xmake.lua"),
            'set_languages("c++23")\nadd_rules("mode.debug", "mode.release")\n' .. stubs(package, {"lattice-tools"}) .. script)
        -- Without its own xmake.lua the nested xmake would climb to a parent project and hang there.
        assert(os.isfile(path.join(build_root, "xmake.lua")), "no sources staged in " .. build_root)
        os.cd(build_root)
        import("package.tools.xmake").install(package, {}, {target = "lattice-logger"})
    end)
    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            void test() { Logger::info("consumer", "standalone logger"); }
        ]]}, {configs = {languages = "c++23"}, includes = "lattice-logger/Logger.hpp"}))
    end)
