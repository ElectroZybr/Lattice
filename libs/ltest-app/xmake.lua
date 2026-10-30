-- ltest: finds test executables, runs them in their own processes, sums the reports.
target("ltest")
    set_kind("binary")
    add_files("main.cpp", "ltest-app/*.cpp")
    add_includedirs(".")
    add_deps("lattice-test", "lattice-logger")
    add_rpathdirs("@loader_path/../lib")

if has_config("lattice_build_extras") then
    target("ltest.probe")
        set_kind("binary")
        set_default(false)
        add_files("probe/*.cpp")
        add_deps("lattice-test-main")

    target("ltest.tests")
        set_kind("binary")
        add_files("tests/*.cpp", "ltest-app/*.cpp")
        add_includedirs(".")
        add_deps("lattice-test-main", "ltest.probe")
        after_load(function (target)
            target:add("defines", 'LTEST_PROBE="' .. path.absolute(target:dep("ltest.probe"):targetfile()):gsub("\\", "/") .. '"')
        end)
end

task("install-ltest")
    set_category("plugin")
    set_menu {
        usage = "xmake install-ltest [options]",
        description = "Install ltest and link it into ~/.local/bin",
        options = {{nil, "prefix", "kv", nil, "Install prefix (default ~/.local/opt/ltest)"}}
    }
    on_run(function ()
        import("core.base.option")
        local userdir = os.getenv("HOME")
        local prefix = option.get("prefix") or path.join(userdir, ".local", "opt", "ltest")
        os.execv(os.programfile(), {"build", "-y", "ltest"})
        os.tryrm(path.join(prefix, "bin", "ltest"))
        os.execv(os.programfile(), {"install", "-y", "-o", prefix, "ltest"})
        local bin = path.join(userdir, ".local", "bin")
        os.mkdir(bin)
        local link = path.join(bin, "ltest")
        os.tryrm(link)
        os.ln(path.join(prefix, "bin", "ltest"), link)
        cprint("${green}ltest${clear} installed in %s, linked as %s", prefix, link)
    end)
