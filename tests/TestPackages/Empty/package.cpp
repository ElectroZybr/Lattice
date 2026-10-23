#include <Lattice/Composition.hpp>

extern "C" constinit const Lattice::Package lattice_package{
    .id = "Empty",
    .version = {1, 0, 0},
};

extern "C" void compose(NodeBuild) {}
