set_project("Lattice")
set_xmakever("2.8.6")
set_languages("c++23")
set_policy("package.requires_lock", true)

add_rules("mode.debug", "mode.release")

add_requires("glm 1.0.1", "toml++ 3.4.0")

includes("Tools/Generators/plugin_codegen.lua")

target("Lattice")
    set_kind("shared")
    set_symbols("hidden")
    add_defines("LATTICE_BUILDING_LIBRARY")

    add_files("Kernel/**.cpp")
    add_files("Tools/**.cpp")

    add_headerfiles("Lattice.hpp", {prefixdir = "Lattice"})
    add_headerfiles("Kernel/(**.hpp)", {prefixdir = "Lattice/Kernel"})
    add_headerfiles("Tools/(**.hpp)", {prefixdir = "Lattice/Tools"})

    add_installfiles(
        "Tools/Generators/plugin_codegen.lua",
        "Tools/Generators/generate_plugin.py",
        {prefixdir = "share/Lattice/codegen"}
    )

    add_includedirs("..", {public = true})

    add_packages("glm", "toml++", {public = true})

    if is_plat("linux") then
        add_syslinks("dl", "pthread", {public = true})
    end

-- сборка тестов
target("Lattice.tests")
    set_kind("shared")

    add_files("tests/*.cpp")
    
    for _, dir in ipairs(os.dirs("tests/TestPlugins/*")) do
        includes(dir)
    end

    add_deps("Lattice")

-- -- публичные инструменты Lattice
-- target("LatticeTools")
--     set_kind("static")
--     set_languages("c++23")
--     add_files("Tools/*.cpp")
--     add_headerfiles("Tools/(Tools/**.hpp)")
--     add_includedirs("Tools/", {public = true})

-- сборка бенчмарков
target("Lattice.bench")
    set_kind("shared")

    add_files("bench/*.cpp")

    add_deps("Lattice")
