#include <Lattice/Composition.hpp>
#include "model.hpp"

using namespace SlotExample;
namespace {
auto composeSoftware(NodeBuild& run) {
    return run.emplace<SoftwareRenderer>("backend", run.require<Palette>("palette"));
}
auto composeHardware(NodeBuild& run) {
    auto queue = run.emplace<UploadQueue>("uploads", run.require<Palette>("palette"));
    return run.emplace<HardwareRenderer>("backend", queue);
}
}

extern "C" void compose(NodeBuild root) {
    root.emplace<Palette>("palette");
    auto renderer = root.addSlot<Renderer>("renderer");
    renderer.choice("SlotExample::Software", composeSoftware);
    root.emplace<Viewport>("viewport", renderer.ref());
}

// External orchestration calls this again after maintain(), while replacement is pending.
// No builder or automatic rebuild job is stored in a component.
bool useHardware(NodeBuild& root) {
    auto renderer = root.addSlot<Renderer>("renderer");
    if (!renderer.choice("SlotExample::Hardware", composeHardware))
        return false;
    root.emplace<Viewport>("viewport", renderer.ref());
    return true;
}
