#include <Lattice/Composition.hpp>
#include "model.hpp"

using namespace SlotExample;
namespace {
auto composeSoftware(NodeBuild& run) {
    return run.construct<SoftwareRenderer, Renderer>(run.require<Palette>("palette"));
}
auto composeHardware(NodeBuild& run) {
    auto queue = run.emplace<UploadQueue>("uploads", run.require<Palette>("palette"));
    return run.construct<HardwareRenderer, Renderer>(queue);
}
}

// What a package lists in `lattice_package.implementations`; main registers it by hand.
constinit const Lattice::Implementation implementations[] = {
    Lattice::implementation<Renderer, composeSoftware>("Software"),
    Lattice::implementation<Renderer, composeHardware>("Hardware"),
};
std::span<const Lattice::Implementation> slotImplementations() { return implementations; }

extern "C" void compose(NodeBuild root) {
    root.emplace<Palette>("palette");
    auto renderer = root.addSlot<Renderer>("renderer");
    renderer.select("Software");          // two implementations: the slot waits for a choice
    root.emplace<Viewport>("viewport", renderer.ref());
}

// External orchestration calls this again after maintain(), while replacement is pending.
// No builder or automatic rebuild job is stored in a component.
bool useHardware(NodeBuild& root) {
    auto renderer = root.addSlot<Renderer>("renderer");
    if (!renderer.select("Hardware"))
        return false;
    root.emplace<Viewport>("viewport", renderer.ref());
    return true;
}
