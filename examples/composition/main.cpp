#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include <Lattice/Composition.hpp>
#include <Lattice/kernel/Context.hpp>
#include <Lattice/kernel/PackageManager.hpp>
#include "model.hpp"

namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Pass the example package directory, or use xmake run.\n";
        return 1;
    }
    // Loaded code must outlive objects and blueprints that refer to it.
    DLLoader libraries;
    Lattice::Context runtime;
    Lattice::PackageManager packages(runtime.blueprints, libraries);
    if (packages.load(argv[1]) != 1)
        return 1;
    auto& nodes = runtime.nodes;
    const auto branch = nodes.builder.add(
        Lattice::InvalidNodeId, runtime.blueprints.resolve("Example"), "demo");

    try {
        const auto slowId = nodes.registry.find("slow", branch);
        const auto fastId = nodes.registry.find("fast", branch);
        check(nodes.dependencies.dependencies(slowId).size() == 3, "Mount dependencies missing");
        auto slow = NodeBuild{slowId, nodes};
        auto fast = NodeBuild{fastId, nodes};
        auto slowSimulation = slow.require<Example::Simulation>("simulation");
        auto fastSimulation = fast.require<Example::Simulation>("simulation");
        auto slowStorage = slow.require<Example::ParticleStorage>("particles");
        auto fastStorage = fast.require<Example::ParticleStorage>("particles");
        auto slowIntegrator = slow.require<Example::Integrator>("integrator");
        auto fastIntegrator = fast.require<Example::Integrator>("integrator");
        auto slowSampler = slow.require<Example::Sampler>("sampler");
        auto fastSampler = fast.require<Example::Sampler>("sampler");

        check(slowStorage.get() != fastStorage.get(), "Particle storage must be local");
        check(&slowIntegrator->device() != &fastIntegrator->device(), "Local focus must override CPU");
        auto root = NodeBuild{branch, nodes};
        auto cpu = root.require<Example::ComputeDevice>("cpu");
        auto gpu = root.require<Example::ComputeDevice>("gpu");
        check(&slowIntegrator->device() == cpu.get(), "Slow must inherit CPU");
        check(&fastIntegrator->device() == gpu.get(), "Fast must select GPU");
        check(&slowIntegrator->field() == &fastIntegrator->field(), "Force field must be shared");
        check(&slowSampler->metrics() == &fastSampler->metrics(), "Metrics must be shared");

        for (int i = 0; i < 4; ++i) {
            slowSimulation->step();
            fastSimulation->step();
        }
        check(std::abs(slowStorage->particles.front().position - 2.625) < 1e-9, "Slow result mismatch");
        check(std::abs(fastStorage->particles.front().position - 1.15625) < 1e-9, "Fast result mismatch");
        std::cout << std::fixed << std::setprecision(5);
        std::cout << "Devices: slow=cpu, fast=gpu; shared field and metrics\n";
        std::cout << "Slow position: " << slowStorage->particles.front().position << '\n';
        std::cout << "Fast position: " << fastStorage->particles.front().position << '\n';

        const auto dashboard = NodeBuild{branch, nodes}.find<Example::Dashboard>("dashboard");
        check(dashboard->sources() == 2 && !dashboard->hasLogger(), "Collection/optional mismatch");
        const auto dashboardId = dashboard.id();

        auto focus = NodeBuild{fastId, nodes}.focus<Example::ComputeDevice>();
        NodeBuild{fastId, nodes}.focus<Example::ComputeDevice>(cpu);
        check(focus.ref().get() == cpu.get(), "New focus must resolve CPU");
        check(&fastIntegrator->device() == gpu.get(), "Existing constructor reference must remain GPU");
        std::cout << "Focus changed to cpu; existing fast still uses gpu\n";

        // Both runs export 'dt', identified by (owner, name).
        check(static_cast<bool>(nodes.exports.set(nodes.exports.find(fastIntegrator.id(), "dt"), 0.5)), "dt write refused");
        check(slowIntegrator->dt == 0.25, "Changing fast dt affected slow dt");

        // Remove a consumer branch. Shared providers and the other run survive.
        nodes.builder.del(branch, slowId);
        nodes.ops.maintain();
        check(!nodes.registry.get(slowId), "Slow branch was not removed");
        check(!nodes.registry.get(dashboardId), "Snapshot consumer must retire with its source");
        std::cout << "Dashboard snapshot retired with slow branch\n";
        fastSimulation->step();
        check(std::abs(fastStorage->particles.front().position - 2.65625) < 1e-9, "Surviving run mismatch");
        check(cpu->submissions == 4 && gpu->submissions == 5, "Device snapshot/counter mismatch");
        check(fastSampler->metrics().samples.size() == 9, "Shared metrics lost samples");
        std::cout << "Fast after removing slow: " << fastStorage->particles.front().position << '\n';
        std::cout << "Submissions cpu/gpu: " << cpu->submissions << '/' << gpu->submissions
                  << "; samples: " << fastSampler->metrics().samples.size() << '\n';
    } catch (...) {
        nodes.ops.destroyBranch(branch);
        throw;
    }
    nodes.ops.retireBranch(branch);
    nodes.ops.collectRetired();
    check(!nodes.registry.get(branch), "Graceful shutdown did not complete");
}
