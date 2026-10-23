#include <Lattice/Composition.hpp>

extern "C" constinit const Lattice::Package lattice_package{
    .id = "CycleA",
    .version = {1, 0, 0},
    .dependencies = {{.id = "CycleB"}},
};

extern "C" void compose(NodeBuild) {}
