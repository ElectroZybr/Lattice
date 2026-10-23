set_project("Lattice.consumer")
set_xmakever("2.8.6")
set_languages("c++23")

add_rules("mode.debug", "mode.release")
add_repositories("lattice-local " .. path.absolute("../..", os.scriptdir()))
add_requires("glm 1.0.1", "lattice-bmrunner")

option("lattice_prefix")
    set_showmenu(true)
    set_description("Path to an installed Lattice prefix")
option_end()

local lattice_prefix = get_config("lattice_prefix") or os.getenv("LATTICE_PREFIX") or ""

target("Lattice.consumer")
    set_kind("binary")
    add_files("main.cpp")

    add_includedirs(path.join(lattice_prefix, "include"))
    add_linkdirs(path.join(lattice_prefix, "lib"))
    add_links("Lattice")
    add_packages("glm", "lattice-bmrunner")

    if is_plat("linux") then
        add_syslinks("dl", "pthread")
        add_rpathdirs(path.join(lattice_prefix, "lib"))
    elseif is_plat("macosx") then
        add_rpathdirs(path.join(lattice_prefix, "lib"))
    end
