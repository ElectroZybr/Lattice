#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/Tools/Tests.hpp>

namespace Lattice::MountTests {

struct Device {
    int value = 7;
};

struct Consumer {
    explicit Consumer(Device& device) : device(device) {}
    Device& device;
};

TEST(Node_CompactLayout, RuntimeFixture,
    "Node должен занимать не больше 64 байт на 64-битной платформе.")
{
    if constexpr (sizeof(void*) == 8)
        REQUIRE(sizeof(Node) <= 64);
}

TEST(Mount_RetainsSharedProvider, RuntimeFixture,
    "mount разрешает общего кандидата через Context, и ветка зависит от него.")
{
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    const auto device = root.emplaceShared<Device>("device");
    auto branch = root.branch("consumer");

    auto mounted = branch.mount<Device>();
    const auto consumer = branch.emplace<Consumer>("consumer", mounted.ref());

    REQUIRE(mounted.target() == device.id());
    REQUIRE(&consumer->device == device.get());
    REQUIRE(nodes.dependencies.retained(branch.id(), device.id()));
    REQUIRE(nodes.dependencies.retained(consumer.id(), device.id()));
}

TEST(Mount_ProviderRetirementRetiresMountingBranch, RuntimeFixture,
    "Удаление общего поставщика завершает ветку, которая его примонтировала.")
{
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    const auto device = root.emplaceShared<Device>("device");
    auto branch = root.branch("consumer");
    const NodeId branchId = branch.id();
    branch.mount<Device>();

    nodes.ops.retireBranch(device.id());
    REQUIRE(nodes.registry.require(branchId).state == NodeState::Retiring);

    nodes.ops.maintain();
    REQUIRE(nodes.registry.get(branchId) == nullptr);
    REQUIRE(nodes.registry.get(device.id()) == nullptr);
}

TEST(Mount_MissingProviderIsAnError, RuntimeFixture,
    "mount без общего кандидата — ошибка, а не создание объекта.")
{
    auto root = fixture.build(fixture.root);
    root.emplace<Device>("local");

    bool thrown = false;
    try {
        root.branch("consumer").mount<Device>();
    } catch (const ExceptionBase&) {
        thrown = true;
    }

    REQUIRE(thrown);
}

}
