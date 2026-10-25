#include <iostream>
#include <stdexcept>
#include <Lattice/Composition.hpp>
#include <Lattice/kernel/Context.hpp>
#include "model.hpp"

extern "C" void compose(NodeBuild);
bool useHardware(NodeBuild&);
std::span<const Lattice::Implementation> slotImplementations();

int main() {
    Lattice::Context runtime;
    auto& nodes = runtime.nodes;
    const auto type = runtime.blueprints.addComposition("SlotExample", compose);
    for (const auto& implementation : slotImplementations())
        runtime.blueprints.addImplementation(implementation, "SlotExample");
    const auto branch = nodes.builder.add(Lattice::InvalidNodeId, type, "demo");
    try {
        auto root = NodeBuild{branch, nodes};
        auto viewport = root.require<SlotExample::Viewport>("viewport");
        if (viewport->frame() != 16) throw std::runtime_error("Software rendering failed");
        std::cout << "Initial renderer: " << viewport->renderer.backend() << '\n';
        if (useHardware(root)) throw std::runtime_error("Old contents must retire first");
        if (root.find<SlotExample::Viewport>("viewport"))
            throw std::runtime_error("Retiring viewport must be hidden");
        std::cout << "Old renderer and viewport retiring\n";
        nodes.ops.maintain();
        if (!useHardware(root)) throw std::runtime_error("Synchronous shutdown did not finish");
        auto replacement = root.require<SlotExample::Viewport>("viewport");
        if (replacement->frame() != 32) throw std::runtime_error("Hardware rendering failed");
        std::cout << "Replacement renderer: " << replacement->renderer.backend()
                  << "; frame=" << replacement->frame() << '\n';
        nodes.ops.retireBranch(branch);
        nodes.ops.maintain();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        nodes.ops.destroyBranch(branch);
        return 1;
    }
}
