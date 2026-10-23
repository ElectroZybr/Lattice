#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/Tools/Tests.hpp>

namespace Lattice {
namespace {
struct FocusDevice { explicit FocusDevice(int value) : value(value) {} int value; };
struct OtherDevice {};
struct FocusUser {
    explicit FocusUser(FocusDevice& device) : device(device) {}
    void stop() { ++stopCalls; }
    bool stopped() const { return ready; }
    FocusDevice& device;
    int stopCalls = 0;
    bool ready = true;
};
void composeFocusedUser(NodeBuild root) {
    auto device = root.mount<FocusDevice>();
    auto user = root.emplace<FocusUser>("user", device.ref());
    user.onRetire(&FocusUser::stop);
    user.readyToDestroy(&FocusUser::stopped);
}
template<class F> bool rejectsFocus(F&& fn) {
    try { fn(); } catch (const std::exception&) { return true; }
    return false;
}
}

TEST(Focus_LocalChoiceInheritanceAndReset, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto cpu = root.emplaceShared<FocusDevice>("cpu", 1);
    auto gpu = root.emplaceShared<FocusDevice>("gpu", 2);
    auto role = root.focus<FocusDevice>();
    REQUIRE(!role);
    REQUIRE(rejectsFocus([&] { role.ref(); }));
    root.focus<FocusDevice>(cpu.ref());
    auto left = root.branch("left");
    auto right = root.branch("right");
    REQUIRE(left.focus<FocusDevice>().ref().get() == cpu.get());
    right.focus<FocusDevice>(gpu.ref());
    auto nested = right.branch("nested");
    REQUIRE(nested.mount<FocusDevice>().ref().get() == gpu.get());
    REQUIRE(left.mount<FocusDevice>().ref().get() == cpu.get());
    REQUIRE(role.ref().get() == cpu.get());
    right.focus<FocusDevice>().reset();
    REQUIRE(nested.focus<FocusDevice>().ref().get() == cpu.get());
    // Merely choosing or resolving a Focus adds no consumer dependency.
    REQUIRE(fixture.run_ctx.nodes.dependencies.dependencies(fixture.root).empty());
    REQUIRE(fixture.run_ctx.nodes.dependencies.dependencies(right.id()).empty());
}

TEST(Focus_ValidatesRuntimeTypeCandidateAndScopeLifetime, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto cpu = root.emplaceShared<FocusDevice>("cpu", 1);
    auto privateBranch = root.branch("private");
    auto unpublished = privateBranch.emplace<FocusDevice>("device", 9);
    REQUIRE(rejectsFocus([&] { root.focus<FocusDevice>(unpublished.ref()); }));
    auto wrong = root.emplaceShared<OtherDevice>("other");
    REQUIRE(rejectsFocus([&] { root.focus<FocusDevice>(wrong.ref()); }));
    RuntimeFixture foreign;
    auto foreignDevice = foreign.build(foreign.root).emplaceShared<FocusDevice>("device", 3);
    REQUIRE(rejectsFocus([&] { root.focus<FocusDevice>(foreignDevice.ref()); }));
    auto local = root.branch("local");
    auto stale = local.focus<FocusDevice>();
    const auto localId = local.id();
    fixture.run_ctx.nodes.ops.destroyBranch(localId);
    auto replacement = root.branch("replacement");
    REQUIRE(replacement.id() == localId);
    replacement.focus<FocusDevice>();
    REQUIRE(!stale.valid());
    REQUIRE(rejectsFocus([&] { local.focus<FocusDevice>(cpu.ref()); }));
    REQUIRE(rejectsFocus([&] { stale.reset(); }));
    REQUIRE(rejectsFocus([&] { stale.ref(); }));
}

TEST(Focus_ChangesFutureResolutionButNotConstructorSnapshots, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto cpu = root.emplaceShared<FocusDevice>("cpu", 1);
    auto gpu = root.emplaceShared<FocusDevice>("gpu", 2);
    auto local = root.branch("local");
    auto focus = local.focus<FocusDevice>();
    local.focus<FocusDevice>(cpu.ref());
    auto snapshot = focus.ref();
    auto existing = local.emplace<FocusUser>("existing", snapshot);
    const auto count = fixture.run_ctx.nodes.registry.children(local.id()).size();
    local.focus<FocusDevice>(gpu.ref());
    REQUIRE(fixture.run_ctx.nodes.registry.children(local.id()).size() == count);
    REQUIRE(snapshot.get() == cpu.get());
    REQUIRE(&existing->device == cpu.get());
    REQUIRE(focus.get() == gpu.get());
    auto next = local.emplace<FocusUser>("next", local.mount<FocusDevice>().ref());
    REQUIRE(&next->device == gpu.get());
    REQUIRE(fixture.run_ctx.nodes.dependencies.retained(existing.id(), cpu.id()));
    REQUIRE(!fixture.run_ctx.nodes.dependencies.retained(existing.id(), gpu.id()));
    REQUIRE(fixture.run_ctx.nodes.dependencies.retained(next.id(), gpu.id()));
}

TEST(Focus_RebuildWaitsForRetirementAndUsesNewSelection, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto& nodes = fixture.run_ctx.nodes;
    auto cpu = root.emplaceShared<FocusDevice>("cpu", 1);
    auto gpu = root.emplaceShared<FocusDevice>("gpu", 2);
    root.focus<FocusDevice>(cpu.ref());
    const auto type = fixture.run_ctx.blueprints.addComposition("FocusedConsumer", composeFocusedUser);
    const auto previous = nodes.builder.add(fixture.root, type, "consumer");
    auto old = NodeBuild{previous, nodes}.require<FocusUser>("user");
    REQUIRE(&old->device == cpu.get());
    root.focus<FocusDevice>(gpu.ref());
    REQUIRE(&old->device == cpu.get());
    old->ready = false;
    // Ensure failure cleanup can always drain the external readiness predicate.
    struct Release { FocusUser* user; ~Release() { if (user) user->ready = true; } } release{old.get()};
    nodes.builder.del(fixture.root, previous);
    REQUIRE(old->stopCalls == 1);
    nodes.ops.maintain();
    REQUIRE(nodes.registry.get(previous));
    REQUIRE(rejectsFocus([&] { nodes.builder.add(fixture.root, type, "consumer"); }));
    old->ready = true;
    release.user = nullptr;
    nodes.ops.maintain();
    REQUIRE(!nodes.registry.get(previous));
    const auto rebuilt = nodes.builder.add(fixture.root, type, "consumer");
    auto current = NodeBuild{rebuilt, nodes}.require<FocusUser>("user");
    REQUIRE(&current->device == gpu.get());
    REQUIRE(current.generation() != old.generation());
    REQUIRE(nodes.registry.get(cpu.id()));
    REQUIRE(nodes.registry.get(gpu.id()));
}

TEST(Focus_ResetRestoresAmbiguityAndRetiringTargetsAreRejected, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto& nodes = fixture.run_ctx.nodes;
    auto missing = root.focus<FocusDevice>();
    REQUIRE(rejectsFocus([&] { missing.ref(); }));
    auto cpu = root.emplaceShared<FocusDevice>("cpu", 1);
    auto gpu = root.emplaceShared<FocusDevice>("gpu", 2);
    auto local = root.branch("local");
    auto focus = local.focus<FocusDevice>();
    local.focus<FocusDevice>(cpu.ref());
    focus.reset();
    REQUIRE(rejectsFocus([&] { focus.ref(); }));
    auto old = cpu.ref();
    nodes.ops.retireBranch(cpu.id());
    REQUIRE(rejectsFocus([&] { local.focus<FocusDevice>(old); }));
    REQUIRE(focus.ref().get() == gpu.get());
    nodes.ops.collectRetired();
}

TEST(Focus_FailedRebuildDoesNotRestoreOldBranch, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto& nodes = fixture.run_ctx.nodes;
    auto cpu = root.emplaceShared<FocusDevice>("cpu", 1);
    root.focus<FocusDevice>(cpu.ref());
    auto type = fixture.run_ctx.blueprints.addComposition("Consumer", composeFocusedUser);
    const auto previous = nodes.builder.add(fixture.root, type, "consumer");
    nodes.builder.del(fixture.root, previous);
    nodes.ops.maintain();
    REQUIRE(!nodes.registry.get(previous));
    auto failing = fixture.run_ctx.blueprints.addComposition("FailedConsumer", +[](NodeBuild branch) {
        branch.emplace<OtherDevice>("partial");
        throw std::runtime_error("failed new composition");
    });
    REQUIRE(rejectsFocus([&] { nodes.builder.add(fixture.root, failing, "consumer"); }));
    REQUIRE(nodes.registry.find("consumer", fixture.root, failing) == InvalidNodeId);
    REQUIRE(nodes.registry.find("consumer", fixture.root, type) == InvalidNodeId);
    REQUIRE(nodes.registry.get(cpu.id()));
}
}
