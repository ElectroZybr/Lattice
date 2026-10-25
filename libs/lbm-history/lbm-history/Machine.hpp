#pragma once

#include <string>

namespace Lattice::BmHistory {

/// The machine a run was measured on: results of different machines are not comparable.
struct Machine {
    /// `<host>-<hash>`: the hash covers processors, caches, memory size, GPUs and their
    /// drivers — new hardware or a new driver is a new folder, not a "regression".
    std::string id;
    /// machine.toml: what the hash was taken of, for people.
    std::string description;
};

Machine currentMachine();

}
