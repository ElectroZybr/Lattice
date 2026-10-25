#include <stdexcept>

#include <Lattice/Lattice.hpp>
#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/tools/Tests.hpp>

namespace Lattice {
namespace {

struct BuilderTestObject {
    inline static int constructed = 0;
    BuilderTestObject() { ++constructed; }
};

void composeObject(::NodeBuild root) {
    root.emplace<BuilderTestObject>("object");
}

void composeFailing(::NodeBuild root) {
    root.emplace<BuilderTestObject>("partial");
    throw std::runtime_error("compose failed");
}

bool failOnce = true;

void composeFailingOnce(::NodeBuild root) {
    root.emplace<BuilderTestObject>("partial");
    if (failOnce) {
        failOnce = false;
        throw std::runtime_error("compose failed");
    }
}

}

TEST(Builder_BuildsBranch, RuntimeFixture,
    "Builder должен собрать ветку композиции.") {
    auto& nodes = fixture.run_ctx.nodes;
    const BlueprintId blueprint = nodes.blueprints.addComposition("BuilderBranch", composeObject);

    const NodeId branch = nodes.builder.add(fixture.root, blueprint, "one");

    REQUIRE(branch != InvalidNodeId);
    REQUIRE(fixture.build(branch).find<BuilderTestObject>("object").exists());
}

TEST(Builder_BuildsRoot, RuntimeFixture,
    "Builder должен уметь собрать корневую ветку без особого пути.") {
    auto& nodes = fixture.run_ctx.nodes;
    const BlueprintId blueprint = nodes.blueprints.addComposition("BuilderRoot", composeObject);

    const NodeId root = nodes.builder.add(InvalidNodeId, blueprint);

    REQUIRE(nodes.registry.require(root).parent == InvalidNodeId);
    REQUIRE(fixture.build(root).find<BuilderTestObject>("object").exists());

    nodes.ops.destroyBranch(root);
}

TEST(Builder_BatchBuildsSeveralBranches, RuntimeFixture,
    "Batch должен собрать все ветки и сохранить их после commit.") {
    auto& nodes = fixture.run_ctx.nodes;
    const BlueprintId blueprint = nodes.blueprints.addComposition("BuilderBatch", composeObject);
    BuilderTestObject::constructed = 0;

    auto batch = nodes.builder.begin();
    const NodeId one = batch.add(fixture.root, blueprint, "one");
    const NodeId two = batch.add(fixture.root, blueprint, "two");
    batch.commit();

    REQUIRE(BuilderTestObject::constructed == 2);
    REQUIRE(nodes.registry.get(one) != nullptr);
    REQUIRE(nodes.registry.get(two) != nullptr);
}

TEST(Builder_ComposeFailureRollsBackBatch, RuntimeFixture,
    "Ошибка композиции должна удалить все ветки текущего batch.") {
    auto& nodes = fixture.run_ctx.nodes;
    const BlueprintId regular = nodes.blueprints.addComposition("BuilderRollbackRegular", composeObject);
    const BlueprintId failing = nodes.blueprints.addComposition("BuilderRollbackFailing", composeFailing);

    bool thrown = false;
    try {
        auto batch = nodes.builder.begin();
        batch.add(fixture.root, regular, "regular");
        batch.add(fixture.root, failing, "failing");
        batch.commit();
    } catch (const std::runtime_error&) {
        thrown = true;
    }

    REQUIRE(thrown);
    REQUIRE(nodes.registry.find("regular", fixture.root, regular) == InvalidNodeId);
    REQUIRE(nodes.registry.find("failing", fixture.root, failing) == InvalidNodeId);
    REQUIRE(nodes.registry.children(fixture.root).empty());
}

TEST(Builder_RejectsExistingBranch, RuntimeFixture,
    "Builder не должен принимать существующую ветку за созданную транзакцией.") {
    auto& nodes = fixture.run_ctx.nodes;
    const BlueprintId blueprint = nodes.blueprints.addComposition("BuilderDuplicate", composeObject);
    nodes.builder.add(fixture.root, blueprint, "same");

    bool thrown = false;
    try {
        nodes.builder.add(fixture.root, blueprint, "same");
    } catch (const ExceptionBase&) {
        thrown = true;
    }

    REQUIRE(thrown);
    REQUIRE(nodes.registry.find("same", fixture.root, blueprint) != InvalidNodeId);
}

TEST(Builder_UncommittedBatchRollsBack, RuntimeFixture,
    "Незавершённый batch должен откатываться при выходе из scope.") {
    auto& nodes = fixture.run_ctx.nodes;
    const BlueprintId blueprint = nodes.blueprints.addComposition("BuilderUncommitted", composeObject);

    {
        auto batch = nodes.builder.begin();
        batch.add(fixture.root, blueprint, "temporary");
    }

    REQUIRE(nodes.registry.find("temporary", fixture.root, blueprint) == InvalidNodeId);
}

TEST(Builder_DelRetiresDirectChild, RuntimeFixture,
    "Builder::del должен завершить непосредственного ребёнка; удаление — после maintain.") {
    auto& nodes = fixture.run_ctx.nodes;
    const BlueprintId blueprint = nodes.blueprints.addComposition("BuilderDel", composeObject);
    const NodeId branch = nodes.builder.add(fixture.root, blueprint, "branch");

    nodes.builder.del(fixture.root, branch);

    REQUIRE(nodes.registry.require(branch).state == NodeState::Retiring);
    nodes.ops.maintain();
    REQUIRE(nodes.registry.get(branch) == nullptr);
}

TEST(Builder_DelRejectsNonDirectChild, RuntimeFixture,
    "Builder::del должен принимать только непосредственного ребёнка указанного owner.") {
    auto& nodes = fixture.run_ctx.nodes;
    const BlueprintId blueprint = nodes.blueprints.addComposition("BuilderDelParent", composeObject);
    const NodeId parent = nodes.builder.add(fixture.root, blueprint, "parent");
    const NodeId child = nodes.registry.find("object", parent, nodes.blueprints.id<BuilderTestObject>());

    bool thrown = false;
    try {
        nodes.builder.del(fixture.root, child);
    } catch (const ExceptionBase&) {
        thrown = true;
    }

    REQUIRE(thrown);
    REQUIRE(nodes.registry.get(child) != nullptr);
    REQUIRE(nodes.registry.require(child).state == NodeState::Active);
}

TEST(Builder_ComposeFailureLeavesNoRegistryGarbageAndCanRetry, RuntimeFixture,
    "Ошибка композиции должна полностью откатить ветку и освободить её имя для повторного add.") {
    auto& nodes = fixture.run_ctx.nodes;
    const BlueprintId blueprint = nodes.blueprints.addComposition("BuilderRetry", composeFailingOnce);
    failOnce = true;
    const size_t childrenBefore = nodes.registry.children(fixture.root).size();

    bool thrown = false;
    try {
        nodes.builder.add(fixture.root, blueprint, "retry");
    } catch (const std::runtime_error&) {
        thrown = true;
    }

    REQUIRE(thrown);
    REQUIRE(nodes.registry.children(fixture.root).size() == childrenBefore);
    REQUIRE(nodes.registry.find("retry", fixture.root, blueprint) == InvalidNodeId);

    const NodeId child = nodes.builder.add(fixture.root, blueprint, "retry");

    REQUIRE(child != InvalidNodeId);
    REQUIRE(nodes.registry.find("retry", fixture.root, blueprint) == child);
    REQUIRE(nodes.registry.children(fixture.root).size() == childrenBefore + 1);
}

}
