#include <Lattice/Lattice.hpp>
#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/Tools/Tests.hpp>

namespace Lattice {
namespace {

struct RetiringTarget {
    inline static bool ready = false;
    inline static size_t retireCalls = 0;
    inline static size_t destroyCalls = 0;

    void retire() { ++retireCalls; }
    bool readyToDestroy() const { return ready; }
    void ping() {}

    ~RetiringTarget() { ++destroyCalls; }
};

struct RetiringConsumer {
    inline static size_t destroyCalls = 0;

    explicit RetiringConsumer(RetiringTarget&) {}
    ~RetiringConsumer() { ++destroyCalls; }
};

struct ImmediatelyRetired {
    inline static size_t destroyCalls = 0;
    ~ImmediatelyRetired() { ++destroyCalls; }
};

}

TEST(Retiring_HidesClosureAndWaitsForReadiness, RuntimeFixture,
    "Retiring должен сразу скрыть dependency closure, но сохранить объекты до readiness.") {
    auto& nodes = fixture.run_ctx.nodes;
    RetiringTarget::ready = false;
    RetiringTarget::retireCalls = 0;
    RetiringTarget::destroyCalls = 0;
    RetiringConsumer::destroyCalls = 0;

    auto root = fixture.build(fixture.root);
    auto targetObject = root.emplace<RetiringTarget>("target");
    targetObject.action("ping", &RetiringTarget::ping);
    targetObject.onRetire([](RetiringTarget& object) { object.retire(); });
    targetObject.readyToDestroy([](const RetiringTarget& object) { return object.readyToDestroy(); });
    const NodeId target = targetObject.id();
    const NodeId consumer = root.emplace<RetiringConsumer>("consumer", targetObject.ref()).id();
    const auto targetBlueprint = nodes.blueprints.id<RetiringTarget>();
    const auto consumerBlueprint = nodes.blueprints.id<RetiringConsumer>();
    REQUIRE(nodes.exports.find(target, "ping") != InvalidExportId);

    nodes.ops.retireBranch(target);
    nodes.ops.retireBranch(target);

    REQUIRE(nodes.registry.require(target).state == NodeState::Retiring);
    REQUIRE(nodes.registry.require(consumer).state == NodeState::Retiring);
    REQUIRE(nodes.query.find(fixture.root, targetBlueprint, "target") == InvalidNodeId);
    REQUIRE(nodes.query.find(fixture.root, consumerBlueprint, "consumer") == InvalidNodeId);
    REQUIRE(nodes.query.resolve(target, targetBlueprint) == nullptr);
    REQUIRE(nodes.exports.find(target, "ping") == InvalidExportId);
    REQUIRE(RetiringTarget::retireCalls == 1);
    REQUIRE(nodes.ops.collectRetired() == 0);
    REQUIRE(nodes.registry.get(target) != nullptr);
    REQUIRE(nodes.registry.get(consumer) != nullptr);
    REQUIRE(RetiringTarget::destroyCalls == 0);
    REQUIRE(RetiringConsumer::destroyCalls == 0);

    RetiringTarget::ready = true;
    REQUIRE(nodes.ops.collectRetired() > 0);
    REQUIRE(nodes.registry.get(target) == nullptr);
    REQUIRE(nodes.registry.get(consumer) == nullptr);
    REQUIRE(RetiringTarget::destroyCalls == 1);
    REQUIRE(RetiringConsumer::destroyCalls == 1);
}

TEST(Retiring_MemberConventionsAreNotLifecycle, RuntimeFixture,
    "Методы retire/readyToDestroy без hooks композиции не участвуют в lifecycle.") {
    auto& nodes = fixture.run_ctx.nodes;
    RetiringTarget::ready = false;
    RetiringTarget::retireCalls = 0;

    const NodeId node = fixture.build(fixture.root).emplace<RetiringTarget>("plain").id();

    nodes.ops.retireBranch(node);
    REQUIRE(RetiringTarget::retireCalls == 0);
    REQUIRE(nodes.ops.collectRetired() > 0);
    REQUIRE(nodes.registry.get(node) == nullptr);
}

TEST(Retiring_ObjectWithoutLifecycleIsCollectedImmediately, RuntimeFixture,
    "Объект без lifecycle hooks должен быть готов к физическому удалению сразу.") {
    auto& nodes = fixture.run_ctx.nodes;
    ImmediatelyRetired::destroyCalls = 0;

    const NodeId node = fixture.build(fixture.root).emplace<ImmediatelyRetired>("immediate").id();

    nodes.ops.retireBranch(node);
    REQUIRE(nodes.registry.get(node) != nullptr);
    REQUIRE(nodes.ops.collectRetired() > 0);
    REQUIRE(nodes.registry.get(node) == nullptr);
    REQUIRE(ImmediatelyRetired::destroyCalls == 1);
}

TEST(Retiring_HostServiceCollectsAtCheckpoint, RuntimeFixture,
    "Host-сервис должен обслуживать retiring queue через idle, без знания lifecycle.") {
    auto& nodes = fixture.run_ctx.nodes;
    ImmediatelyRetired::destroyCalls = 0;

    struct Loop {
        void run(const std::function<void()>& idle) { idle(); }
        void requestStop() {}
    };

    auto root = fixture.build(fixture.root);
    const NodeId victim = root.emplace<ImmediatelyRetired>("victim").id();
    auto loop = root.emplace<Loop>("host");
    loop.service(&Loop::run, &Loop::requestStop);

    nodes.ops.retireBranch(victim);
    REQUIRE(nodes.registry.get(victim) != nullptr);

    auto* service = nodes.ops.service(loop.id());
    REQUIRE(service != nullptr);
    service->enter(nodes.ops);

    REQUIRE(nodes.registry.get(victim) == nullptr);
    REQUIRE(ImmediatelyRetired::destroyCalls == 1);
}

}
