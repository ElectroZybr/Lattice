#include <Lattice/Composition.hpp>
#include "model.hpp"

extern "C" constinit const Lattice::Package lattice_package{
    .id = "Example",
    .name = "Composition example",
    .version = {1, 0, 0},
};

namespace {
void composeRun(NodeBuild& run, double dt) {
    // Context resolves the shared providers. Implementation constructors receive
    // ordinary references, and the branch records its mount dependencies.
    auto device = run.mount<Example::ComputeDevice>();
    auto field = run.mount<Example::ForceField>();
    auto metrics = run.mount<Example::Metrics>();

    auto storage = run.emplace<Example::ParticleStorage>("particles");
    auto integrator = run.emplace<Example::Integrator>(
        "integrator", storage, device.ref(), field.ref(), dt);
    auto sampler = run.emplace<Example::Sampler>(
        "sampler", std::string(run.name()), run.require<Example::ParticleStorage>("particles"), metrics.ref());
    auto simulation = run.emplace<Example::Simulation>("simulation", integrator, sampler);

    integrator.param("dt", &Example::Integrator::dt);
    simulation.action("step", &Example::Simulation::step);
}
}

extern "C" void compose(NodeBuild root) {
    auto cpu = root.emplaceShared<Example::ComputeDevice>("cpu", "cpu");
    auto gpu = root.emplaceShared<Example::ComputeDevice>("gpu", "gpu");
    root.emplaceShared<Example::ForceField>("field", 1.0);
    root.emplaceShared<Example::Metrics>("metrics");

    // Descendants inherit CPU unless their own scope makes another choice.
    root.focus<Example::ComputeDevice>(cpu.ref());
    auto slow = root.branch("slow");
    composeRun(slow, 0.25);
    auto fast = root.branch("fast");
    fast.focus<Example::ComputeDevice>(gpu.ref());
    composeRun(fast, 0.125);

    // A snapshot from the whole subtree, plus an optional dependency.
    root.emplace<Example::Dashboard>("dashboard", root.collect<Example::Sampler>(), root.find<Example::Logger>("logger"));
}
