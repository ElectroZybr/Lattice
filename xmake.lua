set_project("Lattice")
-- The release: every library of the repository has this version (soname), the tag is v<version>.
set_version("1.0.0")
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
    -- lattice-logger and lattice-sysinfo as installed packages, written by the package recipe.
    includes("packaged.lua")
    if has_config("lattice_build_extras") then
        add_requires("lbm-runner", "lattice-test")
    end
else
    includes("libs/ltest-app", "libs/lattice-test", "libs/lattice-tools", "libs/lattice-logger", "libs/lattice-sysinfo", "libs/lbm-runner", "libs/lbm-history", "libs/lbm-app")
end

target("Lattice")
    set_kind("shared")
    set_version("1.0.0", {soname = true})
    set_symbols("hidden")
    add_defines("LATTICE_BUILDING_LIBRARY")

    add_files("kernel/**.cpp")
    add_deps("lattice-logger", "lattice-sysinfo", {public = true})
    -- Finds its shared deps beside itself: Ubuntu links with --as-needed, so a binary may not list them.
    add_rpathdirs("@loader_path")

    add_headerfiles("Lattice.hpp", "Composition.hpp", {prefixdir = "Lattice"})
    add_headerfiles("kernel/(**.hpp)", {prefixdir = "Lattice/kernel"})

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

        add_files("tests/*.cpp")
        -- Forward slashes: Windows takes them, and a backslash would escape in the string literal.
        add_defines('LATTICE_TEST_PACKAGES="' .. path.absolute("tests/TestPackages"):gsub("\\", "/") .. '"')

        for _, dir in ipairs(os.dirs("tests/TestPackages/*")) do
            includes(dir)
        end

        add_deps("Lattice")
        if has_config("lattice_external_tools") then
            add_packages("lattice-test")
        else
            add_deps("lattice-test-main")
        end
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
