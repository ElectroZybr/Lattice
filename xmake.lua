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
        add_requires("lattice-bmrunner")
    end
else
    includes("Libs/Tools", "Libs/Logger", "Libs/SysInfo", "Libs/BmRunner", "Libs/BmHistory", "Libs/Lbm")
end

target("Lattice")
    set_kind("shared")
    set_version("0.1.0", {soname = true})
    set_symbols("hidden")
    add_defines("LATTICE_BUILDING_LIBRARY")

    add_files("Kernel/**.cpp")
    add_files("Tools/*.cpp")
    if has_config("lattice_external_tools") then
        add_packages("lattice-logger", "lattice-sysinfo", {public = true})
    else
        add_deps("Logger", "SysInfo", {public = true})
    end

    add_headerfiles("Lattice.hpp", "Composition.hpp", {prefixdir = "Lattice"})
    add_headerfiles("Kernel/(**.hpp)", {prefixdir = "Lattice/Kernel"})
    add_headerfiles("Tools/(**.hpp)", {prefixdir = "Lattice/Tools"})

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
        set_kind("shared")

        add_files("tests/*.cpp")

        for _, dir in ipairs(os.dirs("tests/TestPackages/*")) do
            includes(dir)
        end

        add_deps("Lattice")
        if has_config("lattice_external_tools") then
            add_packages("lattice-bmrunner")
        else
            add_deps("BmRunner")
        end

    -- сборка бенчмарков
    target("Lattice.bench")
        set_kind("shared")

        add_files("bench/*.cpp")

        add_deps("Lattice")
        if has_config("lattice_external_tools") then
            add_packages("lattice-bmrunner")
        else
            add_deps("BmRunner")
        end
end

-- Existing utility tests linked directly to tooling, with no runtime.
if has_config("lattice_build_extras") and not has_config("lattice_external_tools") then
    for name, files in pairs({
        Logger = {"LoggerScope"},
        SysInfo = {"SysInfoOutput"},
        BmRunner = {"BmRunner", "BmOutput", "Analysis", "Cache", "PerfEvent", "MemoryTraffic", "Trace", "Results"},
        BmHistory = {"BmHistory"}
    }) do
        target(name .. ".tests")
            set_kind("binary")
            add_files("Libs/tests/main.cpp", "Tools/Tests.cpp")
            add_defines("LATTICE_BUILDING_LIBRARY")
            add_includedirs("..")
            add_deps(name)
            for _, file in ipairs(files) do
                add_files("tests/" .. file .. "_tests.cpp")
            end
    end
end
