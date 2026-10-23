#include <Lattice/Composition.hpp>

extern "C" constinit const Lattice::Package lattice_package{
    .id = "Consumer",
    .version = {1, 0, 0},
    .dependencies = {{.id = "Provider", .min = {1, 0, 0}}},
};

extern "C" void compose(NodeBuild) {}
