#include <Lattice/Composition.hpp>

extern "C" constinit const Lattice::Package lattice_package{
    .id = "NoCompose",
    .version = {1, 0, 0},
};

// Metadata only: no compose entry point, so loading fails.
