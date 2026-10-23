-- сборка тестового пакета
target("TestCycleA")
    set_kind("shared")
    set_targetdir(".")
    add_files("package.cpp")
    add_includedirs("..", {public = true})
    add_deps("Lattice")