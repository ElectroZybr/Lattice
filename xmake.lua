set_project("Lattice")
set_xmakever("2.8.6")
set_languages("c++23")
set_policy("package.requires_lock", true)

add_rules("mode.debug", "mode.release")

add_requires("glm 1.0.1")

option("lattice_build_extras")
    set_default(true)
    set_showmenu(true)
    set_description("Build Lattice tests and benchmarks")
option_end()

option("lattice_external_tools")
    set_default(false)
    set_description("Use separately installed tooling packages")
option_end()

if has_config("lattice_external_tools") then
    add_requires("lattice-logger", "lattice-sysinfo")
    if has_config("lattice_build_extras") then
        add_requires("lbm-runner")
    end
else
    includes("libs/lattice-tools", "libs/lattice-logger", "libs/lattice-sysinfo", "libs/lbm-runner", "libs/lbm-history", "libs/lbm")
end

target("Lattice")
    set_kind("shared")
    set_version("0.1.0", {soname = true})
    set_symbols("hidden")
    add_defines("LATTICE_BUILDING_LIBRARY")

    add_files("kernel/**.cpp")
    add_files("tools/*.cpp")
    if has_config("lattice_external_tools") then
        add_packages("lattice-logger", "lattice-sysinfo", {public = true})
    else
        add_deps("lattice-logger", "lattice-sysinfo", {public = true})
    end

    add_headerfiles("Lattice.hpp", "Composition.hpp", {prefixdir = "Lattice"})
    add_headerfiles("kernel/(**.hpp)", {prefixdir = "Lattice/kernel"})
    add_headerfiles("tools/(**.hpp)", {prefixdir = "Lattice/tools"})

    add_includedirs("..", {public = true})

    add_packages("glm", {public = true})

    if is_plat("linux") then
        add_syslinks("dl", "pthread", {public = true})
    end

if has_config("lattice_build_extras") then
    includes("examples/composition")
    includes("examples/slot")
    -- сборка тестов
    target("Lattice.tests")
        set_kind("binary")

        add_files("tests/*.cpp", "libs/tests/main.cpp")

        for _, dir in ipairs(os.dirs("tests/TestPackages/*")) do
            includes(dir)
        end

        add_deps("Lattice")
        if has_config("lattice_external_tools") then
            add_packages("lbm-runner")
        else
            add_deps("lbm-runner")
        end

    -- сборка бенчмарков
    target("Lattice.bench")
        set_kind("binary")

        add_files("bench/*.cpp")

        add_deps("Lattice")
        if has_config("lattice_external_tools") then
            add_packages("lbm-runner")
        else
            add_deps("lbm-runner")
        end
end
