#include <algorithm>

#include <Lattice/Lattice.hpp>
#include <Lattice/tests/RuntimeFixture.hpp>
#include <lattice-test/Tests.hpp>

namespace Lattice {
namespace {

struct DependencyTarget {};

struct DependencyConsumer {
    explicit DependencyConsumer(DependencyTarget& target) : target(target) {}
    DependencyTarget& target;
};

struct IndependentObject {};

struct OwnedDependencyChild {};

struct TransitiveDependencyConsumer {
    explicit TransitiveDependencyConsumer(DependencyConsumer&) {}
};

}

TEST(DependencyGraph_StoresBothDirectionsAndDeduplicates, RuntimeFixture,
    "DependencyGraph должен хранить обе стороны одного уникального ребра.") {
    DependencyGraph graph;

    graph.add(3, 7);
    graph.add(3, 7);

    REQUIRE(graph.dependencies(3).size() == 1);
    REQUIRE(graph.dependencies(3).front() == 7);
    REQUIRE(graph.dependents(7).size() == 1);
    REQUIRE(graph.dependents(7).front() == 3);

    graph.add(11, 3);
    graph.clearDependencies(3);

    REQUIRE(graph.dependencies(3).empty());
    REQUIRE(graph.dependents(7).empty());
    REQUIRE(graph.dependencies(11).size() == 1);
    REQUIRE(graph.dependents(3).size() == 1);

    graph.removeNode(3);

    REQUIRE(graph.dependencies(3).empty());
    REQUIRE(graph.dependents(3).empty());
    REQUIRE(graph.dependencies(11).empty());
}

TEST(DependencyGraph_ConstructorArgumentRegistersDependency, RuntimeFixture,
    "Handle в аргументе emplace должен зарегистрировать зависимость потребителя от цели.") {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);

    const auto targetObject = root.emplace<DependencyTarget>("target");
    const NodeId target = targetObject.id();
    const NodeId consumer = root.emplace<DependencyConsumer>("consumer", targetObject.ref()).id();

    REQUIRE(nodes.dependencies.dependencies(consumer).size() == 1);
    REQUIRE(nodes.dependencies.dependencies(consumer).front() == target);
    REQUIRE(nodes.dependencies.dependents(target).size() == 1);
    REQUIRE(nodes.dependencies.dependents(target).front() == consumer);
    REQUIRE(nodes.dependencies.retained(consumer, target));

    // Queries are snapshots and do not add edges.
    root.require<DependencyTarget>("target");

    REQUIRE(nodes.dependencies.dependencies(consumer).size() == 1);
    REQUIRE(nodes.dependencies.dependents(target).size() == 1);
}

TEST(DependencyGraph_DestroyRemovesEdgesBeforeNodeIdReuse, RuntimeFixture,
    "Уничтожение узла должно очистить граф до повторного использования его NodeId.") {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);

    const auto targetObject = root.emplace<DependencyTarget>("target");
    const NodeId target = targetObject.id();
    const NodeId consumer = root.emplace<DependencyConsumer>("consumer", targetObject.ref()).id();

    nodes.ops.destroyBranch(consumer);

    REQUIRE(nodes.dependencies.dependencies(consumer).empty());
    REQUIRE(nodes.dependencies.dependents(consumer).empty());
    REQUIRE(nodes.dependencies.dependents(target).empty());

    const NodeId replacement = root.emplace<IndependentObject>("replacement").id();

    REQUIRE(replacement == consumer);
    REQUIRE(nodes.dependencies.dependencies(replacement).empty());
    REQUIRE(nodes.dependencies.dependents(replacement).empty());
}

TEST(DependencyGraph_RemovalClosureIncludesOwnedAndTransitiveDependents, RuntimeFixture,
    "Removal closure должен рекурсивно объединять ownership-поддеревья и dependents.") {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);

    const auto targetObject = root.emplace<DependencyTarget>("target");
    const NodeId target = targetObject.id();
    const auto consumerObject = root.emplace<DependencyConsumer>("consumer", targetObject.ref());
    const NodeId consumer = consumerObject.id();
    const NodeId ownedChild = ::NodeBuild{consumer, nodes}.emplace<OwnedDependencyChild>("").id();
    const NodeId transitive = root.emplace<TransitiveDependencyConsumer>("transitive", consumerObject.ref()).id();
    const NodeId independent = root.emplace<IndependentObject>("independent").id();
    nodes.dependencies.add(target, transitive); // замыкает цикл target -> transitive -> consumer -> target

    const auto closure = nodes.ops.collectRemovalClosure(target);
    const auto contains = [&closure](NodeId node) {
        return std::ranges::find(closure, node) != closure.end();
    };

    REQUIRE(contains(target));
    REQUIRE(contains(consumer));
    REQUIRE(contains(ownedChild));
    REQUIRE(contains(transitive));
    REQUIRE(!contains(independent));
    REQUIRE(closure.size() == 4);
}

}
