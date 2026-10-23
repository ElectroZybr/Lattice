#include <Lattice/Composition.hpp>

extern "C" constinit const Lattice::Package lattice_package{
    .id = "CycleB",
    .version = {1, 0, 0},
    .dependencies = {{.id = "CycleA"}},
};

extern "C" void compose(NodeBuild) {}
