-- lattice-benchmarks (lbm): benchmark runs, their git history, comparison and regressions
-- (docs/ru/articles/BmHistory.md).
target("lbm")
    set_kind("binary")
    add_files("lbm/*.cpp")
    add_deps("lbm-history")
    -- Installed with its shared libraries next to it: <prefix>/bin/lbm, <prefix>/lib/*.so.
    add_rpathdirs("@loader_path/../lib")

-- xmake install-lbm [--prefix=dir]: lbm in its own prefix (default
-- ~/.local/opt/lattice-benchmarks) and a link in ~/.local/bin, so its libraries do not mix
-- with others in ~/.local/lib.
task("install-lbm")
    set_category("plugin")
    set_menu {
        usage = "xmake install-lbm [options]",
        description = "Install lattice-benchmarks (lbm) and link it into ~/.local/bin",
        options = {{nil, "prefix", "kv", nil, "Install prefix (default ~/.local/opt/lattice-benchmarks)"}}
    }
    on_run(function ()
        import("core.base.option")
        local home = os.getenv("HOME")
        local prefix = option.get("prefix") or path.join(home, ".local", "opt", "lattice-benchmarks")
        os.execv(os.programfile(), {"build", "-y", "lbm"})
        -- Remove before copying: a running lbm keeps its file, the copy does not fail
        -- with "text file busy".
        os.tryrm(path.join(prefix, "bin", "lbm"))
        os.execv(os.programfile(), {"install", "-y", "-o", prefix, "lbm"})
        local bin = path.join(home, ".local", "bin")
        os.mkdir(bin)
        local link = path.join(bin, "lbm")
        os.tryrm(link)
        os.ln(path.join(prefix, "bin", "lbm"), link)
        cprint("${green}lbm${clear} installed in %s, linked as %s", prefix, link)
    end)
