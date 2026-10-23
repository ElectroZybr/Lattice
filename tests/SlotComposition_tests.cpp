#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/Tools/Tests.hpp>

namespace Lattice {
namespace {
struct ComposedAPI { virtual ~ComposedAPI() = default; virtual int value() const = 0; };
struct Gate { bool ready = true; int stops = 0; };
struct ComposedState { int value; explicit ComposedState(int value) : value(value) {} };
struct ComposedA : ComposedAPI {
    ComposedState& state;
    Gate& gate;
    ComposedA(ComposedState& state, Gate& gate) : state(state), gate(gate) {}
    int value() const override { return state.value; }
    void stop() { ++gate.stops; }
    bool stopped() const { return gate.ready; }
};
struct ComposedB : ComposedAPI { int value() const override { return 2; } };
struct ComposedConsumer {
    ComposedAPI& api;
    explicit ComposedConsumer(ComposedAPI& api) : api(api) {}
};
ObjectBuild<ComposedA> composeA(NodeBuild& run) {
    auto state = run.emplace<ComposedState>("state", 1);
    auto object = run.emplace<ComposedA>("backend", state, run.require<Gate>("gate"));
    object.onRetire(&ComposedA::stop);
    object.readyToDestroy(&ComposedA::stopped);
    return object;
}
ObjectBuild<ComposedB> composeB(NodeBuild& run) { return run.emplace<ComposedB>("backend"); }
Ref<ComposedAPI> composeAPI(NodeBuild& run) {
    auto object = run.emplace<ComposedB, ComposedAPI>("backend");
    return {object.get(), object.runtime(), object.id(), object.generation()};
}
ObjectBuild<ComposedB> composeFail(NodeBuild& run) {
    auto partial = run.emplace<ComposedState>("partial", 3);
    run.param("partial", partial->value);
    run.mount<Gate>();
    throw std::runtime_error("composition failed");
}
Ref<ComposedA> composeOutside(NodeBuild& run) { return run.require<ComposedA>("outside"); }
Ref<ComposedA> foreignObject;
Ref<ComposedA> composeForeign(NodeBuild&) { return foreignObject; }
template<class F> bool failsSlotComposition(F&& fn) {
    try { fn(); } catch (const std::exception&) { return true; }
    return false;
}
}

TEST(SlotComposition_InferredAPIAndSnapshots, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    root.emplace<Gate>("gate");
    auto slot = root.addSlot<ComposedAPI>("renderer");
    REQUIRE(slot.choice("SlotExample::A", composeA));
    REQUIRE(slot->value() == 1);
    auto ref = slot.ref();
    REQUIRE(ref.id() != slot.id());
    REQUIRE(root.require<ComposedAPI>("renderer").id() == ref.id());
    REQUIRE(root.find<ComposedAPI>("renderer").id() == ref.id());
    REQUIRE(root.collect<ComposedAPI>().size() == 1);
    REQUIRE(root.focus<ComposedAPI>().ref().id() == ref.id());
    REQUIRE(fixture.run_ctx.nodes.registry.require(slot.id()).object.ptr == nullptr);
    const auto count = fixture.run_ctx.nodes.registry.children(slot.id()).size();
    REQUIRE(slot.choice("SlotExample::A", composeA));
    REQUIRE(slot.ref().id() == ref.id());
    REQUIRE(fixture.run_ctx.nodes.registry.children(slot.id()).size() == count);
    REQUIRE(failsSlotComposition([&] { slot.choice("SlotExample::A", composeB); }));
    REQUIRE(slot->value() == 1);
}

TEST(SlotComposition_RetiresConsumersAndWaitsWithoutBlocking, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    auto gate = root.emplace<Gate>("gate");
    auto slot = root.addSlot<ComposedAPI>("renderer");
    REQUIRE(slot.choice("SlotExample::A", composeA));
    const auto snapshot = slot.ref();
    auto consumer = root.emplace<ComposedConsumer>("consumer", snapshot);
    gate->ready = false;
    struct Release { Gate* gate; ~Release() { gate->ready = true; } } release{gate.get()};
    REQUIRE(!slot.choice("SlotExample::B", composeB));
    REQUIRE(!slot);
    REQUIRE(slot.valid());
    REQUIRE(nodes.registry.require(consumer.id()).state == NodeState::Retiring);
    REQUIRE(gate->stops == 1);
    nodes.ops.maintain();
    REQUIRE(nodes.registry.get(consumer.id()));
    REQUIRE(!slot.choice("SlotExample::B", composeB));
    REQUIRE(gate->stops == 1);
    gate->ready = true;
    nodes.ops.maintain();
    REQUIRE(!nodes.registry.get(consumer.id()));
    REQUIRE(slot.choice("SlotExample::B", composeB));
    REQUIRE(slot->value() == 2);
    REQUIRE(failsSlotComposition([&] { root.emplace<ComposedConsumer>("stale", snapshot); }));
    auto replacement = root.emplace<ComposedConsumer>("consumer", slot.ref());
    REQUIRE(replacement->api.value() == 2);
}

TEST(SlotComposition_FailureLeavesEmptyRecoverableSlot, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    root.emplaceShared<Gate>("gate");
    auto slot = root.addSlot<ComposedAPI>();
    REQUIRE(slot.choice("SlotExample::A", composeA));
    REQUIRE(!slot.choice("SlotExample::Fail", composeFail));
    nodes.ops.maintain();
    REQUIRE(failsSlotComposition([&] { slot.choice("SlotExample::Fail", composeFail); }));
    REQUIRE(!slot);
    REQUIRE(nodes.registry.children(slot.id()).empty());
    REQUIRE(nodes.registry.require(slot.id()).object.bp == InvalidBlueprintId);
    REQUIRE(nodes.exports.find(slot.id(), "partial") == InvalidExportId);
    REQUIRE(nodes.dependencies.dependencies(slot.id()).empty());
    REQUIRE(nodes.context.findScope(slot.id()) == InvalidContextScopeId);
    REQUIRE(slot.choice("SlotExample::B", composeB));
    REQUIRE(slot->value() == 2);
}

TEST(SlotComposition_RejectsUnownedAndForeignResults, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto gate = root.emplace<Gate>("gate");
    auto state = root.emplace<ComposedState>("state", 3);
    root.emplace<ComposedA>("outside", state, gate);
    auto slot = root.addSlot<ComposedAPI>();
    REQUIRE(failsSlotComposition([&] { slot.choice("SlotExample::Outside", composeOutside); }));
    REQUIRE(!slot);
    RuntimeFixture other;
    auto otherRoot = other.build(other.root);
    auto otherGate = otherRoot.emplace<Gate>("gate");
    auto otherState = otherRoot.emplace<ComposedState>("state", 4);
    foreignObject = otherRoot.emplace<ComposedA>("outside", otherState, otherGate).ref();
    REQUIRE(failsSlotComposition([&] { slot.choice("SlotExample::Foreign", composeForeign); }));
    REQUIRE(!slot);
    foreignObject = {};
}

TEST(SlotComposition_StaleSlotAndAncestorConsumerAreRejected, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    root.emplace<Gate>("gate");
    auto slot = root.addSlot<ComposedAPI>();
    REQUIRE(slot.choice("SlotExample::A", composeA));
    // A mount retains the slot from its owner; replacing it must not retire that owner.
    root.mount<ComposedAPI>();
    REQUIRE(failsSlotComposition([&] { slot.choice("SlotExample::B", composeB); }));
    REQUIRE(slot->value() == 1);
    nodes.dependencies.clearDependencies(fixture.root);
    const auto id = slot.id();
    nodes.ops.destroyBranch(id);
    auto replacement = root.addSlot<ComposedAPI>("replacement");
    REQUIRE(replacement.id() == id);
    REQUIRE(!slot.valid());
    REQUIRE(!slot);
    REQUIRE(failsSlotComposition([&] { slot.choice("SlotExample::B", composeB); }));
    REQUIRE(replacement.choice("SlotExample::B", composeB));
}
TEST(SlotComposition_APIReturnAndBlueprintReuseAcrossRuntimes, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    auto first = root.addSlot<ComposedAPI>("first");
    REQUIRE(first.choice("SlotExample::API", composeAPI));
    auto second = root.addSlot<ComposedAPI>("second");
    REQUIRE(second.choice("SlotExample::API", composeAPI));
    REQUIRE(first.ref().id() != second.ref().id());
    REQUIRE(first->value() == 2);
    // A shared blueprint registry must not capture one NodeSystem's address.
    NodeSystem other(fixture.run_ctx.blueprints);
    const auto otherRoot = other.factory.folder(InvalidNodeId, "other");
    auto third = NodeBuild{otherRoot, other}.addSlot<ComposedAPI>();
    REQUIRE(third.choice("SlotExample::API", composeAPI));
    REQUIRE(third.ref().runtime() == &other);
    REQUIRE(third->value() == 2);
    REQUIRE(fixture.run_ctx.blueprints.require(fixture.run_ctx.blueprints.id<ComposedAPI>()).bases.empty());
    other.ops.destroyBranch(otherRoot);
}

}
