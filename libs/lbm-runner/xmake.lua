add_requires("indicators 2.3")

target("lbm-runner")
    set_kind("shared")
    set_version("0.1.0", {soname = true})
    set_symbols("hidden")
    add_defines("BMRUNNER_BUILDING_LIBRARY")
    add_files("lbm-runner/**.cpp|Metrics/platform/*.cpp")
    if is_plat("linux") then
        add_files("lbm-runner/Metrics/platform/Linux.cpp")
    else
        add_files("lbm-runner/Metrics/platform/Unsupported.cpp")
    end
    add_headerfiles("(lbm-runner/**.hpp)")
    add_includedirs(".", {public = true})
    add_packages("indicators")
    add_deps("lattice-logger", "lattice-sysinfo", {public = true})
    if is_plat("linux") then
        add_syslinks("pthread", {public = true})
    end

-- Tests of the library without the runtime: xmake run lbm-runner.tests
if has_config("lattice_build_extras") then
    target("lbm-runner.output-probe")
        set_kind("binary")
        set_default(false)
        add_files("tests/console/Probe.cpp")
        add_deps("lbm-runner")

    target("lbm-runner.tests")
        set_kind("binary")
        add_files("tests/*_tests.cpp")
        add_deps("lbm-runner", "lattice-test-main")
end
