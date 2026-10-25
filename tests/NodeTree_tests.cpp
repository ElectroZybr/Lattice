#include <Lattice/tests/RuntimeFixture.hpp>
#include <lattice-test/Tests.hpp>

#include <Lattice/Lattice.hpp>


namespace Lattice {

class TestComponent {
public:
    int value = 0;
};

class TestAPI {
public:
    virtual ~TestAPI() = default;
};

class TestImplA : public TestAPI {
public:
    int value = 10;
};

class TestImplB : public TestAPI {
public:
    int value = 20;
};


TEST(Node_DeepTreeLookup, RuntimeFixture, 
"Поиск компонента должен подниматься по дереву родителей, но не заходить в соседние ветки. \
Child-ветка должна видеть свои компоненты и компоненты предков.")
{
    auto& nodes = fixture.run_ctx.nodes;

    fixture.build(fixture.root).emplace<TestComponent>("root");

    const NodeId branchA = nodes.factory.folder(fixture.root, "BranchA");
    fixture.build(branchA).emplace<TestComponent>("a");

    const NodeId branchB = nodes.factory.folder(fixture.root, "BranchB");
    fixture.build(branchB).emplace<TestComponent>("b");

    const NodeId branchAChild = nodes.factory.folder(branchA, "BranchAChild");
    fixture.build(branchAChild).emplace<TestComponent>("child");

    REQUIRE(fixture.build(fixture.root).find<TestComponent>("root").exists());
    REQUIRE(fixture.build(branchA).find<TestComponent>("root").exists());
    REQUIRE(fixture.build(branchAChild).find<TestComponent>("root").exists());

    REQUIRE(fixture.build(branchA).find<TestComponent>("a").exists());
    REQUIRE(fixture.build(branchAChild).find<TestComponent>("a").exists());

    REQUIRE(fixture.build(branchB).find<TestComponent>("b").exists());
    REQUIRE(!fixture.build(branchA).find<TestComponent>("b").exists());
    REQUIRE(!fixture.build(fixture.root).find<TestComponent>("missing").exists());
}

TEST(Node_TreeShadowing, RuntimeFixture, 
"Компонент в дочерней ветке должен скрывать компонент с тем же именем из родительской ветки. \
При этом оба объекта должны оставаться независимыми экземплярами.")
{
    auto& nodes = fixture.run_ctx.nodes;

    fixture.build(fixture.root).emplace<TestComponent>("shared");

    const NodeId branch = nodes.factory.folder(fixture.root, "branch");
    fixture.build(branch).emplace<TestComponent>("shared");

    auto rootComponent = fixture.build(fixture.root).require<TestComponent>("shared");
    auto branchComponent = fixture.build(branch).require<TestComponent>("shared");

    REQUIRE(rootComponent);
    REQUIRE(branchComponent);
    REQUIRE(rootComponent.get() != branchComponent.get());
}

TEST(Node_ShadowingDoesNotLeak, RuntimeFixture, 
"Одинаковые имена компонентов в соседних ветках не должны влиять друг на друга. \
Поиск из одной ветки не должен случайно находить локальный компонент другой ветки.")
 {
    auto& nodes = fixture.run_ctx.nodes;

    fixture.build(fixture.root).emplace<TestComponent>("shared");

    const NodeId branchA = nodes.factory.folder(fixture.root, "A");
    const NodeId branchB = nodes.factory.folder(fixture.root, "B");

    fixture.build(branchA).emplace<TestComponent>("shared");

    auto a = fixture.build(branchA).find<TestComponent>("shared");
    auto b = fixture.build(branchB).find<TestComponent>("shared");

    REQUIRE(a.exists());
    REQUIRE(b.exists());
    REQUIRE(a.get() != b.get());
}

TEST(Node_TreeFolderCollect, RuntimeFixture,
    "Поиск в папке должен возвращать компоненты из текущей папки и всех вложенных папок.") {
    auto& nodes = fixture.run_ctx.nodes;

    fixture.build(fixture.root).emplace<TestComponent>("root");

    const NodeId branch = nodes.factory.folder(fixture.root, "branch");
    fixture.build(branch).emplace<TestComponent>("a");
    fixture.build(branch).emplace<TestComponent>("b");

    const NodeId child = nodes.factory.folder(branch, "child");
    fixture.build(child).emplace<TestComponent>("c");

    const NodeId nested = nodes.factory.folder(child, "nested");
    fixture.build(nested).emplace<TestComponent>("d");

    auto root = nodes.query.collect(fixture.root, typeKey<TestComponent>());
    auto branchNode = nodes.query.collect(branch, typeKey<TestComponent>());
    auto childNode = nodes.query.collect(child, typeKey<TestComponent>());
    auto nestedNode = nodes.query.collect(nested, typeKey<TestComponent>());

    REQUIRE(root.size() == 5);
    REQUIRE(branchNode.size() == 4);
    REQUIRE(childNode.size() == 2);
    REQUIRE(nestedNode.size() == 1);
}


TEST(Node_DirectChildren, RuntimeFixture,
    "children возвращает непосредственных детей, включая папки.") {
    auto& nodes = fixture.run_ctx.nodes;

    fixture.build(fixture.root).emplace<TestComponent>("root");

    const NodeId branch = nodes.factory.folder(fixture.root, "branch");
    fixture.build(branch).emplace<TestComponent>("a");
    fixture.build(branch).emplace<TestComponent>("b");

    const NodeId child = nodes.factory.folder(branch, "child");
    fixture.build(child).emplace<TestComponent>("c");

    const NodeId nested = nodes.factory.folder(child, "nested");
    fixture.build(nested).emplace<TestComponent>("d");

    auto root = nodes.registry.children(fixture.root);
    auto branchNode = nodes.registry.children(branch);
    auto childNode = nodes.registry.children(child);
    auto nestedNode = nodes.registry.children(nested);

    REQUIRE(root.size() == 2);
    REQUIRE(branchNode.size() == 3);
    REQUIRE(childNode.size() == 2);
    REQUIRE(nestedNode.size() == 1);
}

TEST(TreeView_ReadOnlyNavigation, RuntimeFixture,
    "TreeView должен возвращать стабильные id и скопированные подписи без доступа к NodeRegistry.") {
    auto& nodes = fixture.run_ctx.nodes;

    const NodeId branch = nodes.factory.folder(fixture.root, "branch");
    fixture.build(branch).emplace<TestComponent>("instance");

    const TreeView tree = fixture.build(branch).tree();
    const auto children = tree.children(branch);

    REQUIRE(tree.contains(fixture.root));
    REQUIRE(tree.root(branch) == fixture.root);
    REQUIRE(tree.parent(branch) == fixture.root);
    REQUIRE(tree.label(branch) == "branch");
    REQUIRE(tree.path(branch) == "Root/branch");
    REQUIRE(children.size() == 1);
    REQUIRE(tree.label(children.front()) == "TestComponent:instance");
    REQUIRE(tree.path(children.front()) == "Root/branch/TestComponent:instance");

    const TreeNodeInfo component = tree.info(children.front());
    REQUIRE(component.id == children.front());
    REQUIRE(component.parent == branch);
    REQUIRE(component.kind == NodeKind::Object);
    REQUIRE(component.name == "instance");
    REQUIRE(component.type == "TestComponent");
    REQUIRE(component.blueprint == nodes.blueprints.id<TestComponent>());
    REQUIRE(component.implementationBlueprint == nodes.blueprints.id<TestComponent>());
    REQUIRE(component.hasObject);

    std::vector<TreeEntry> subtree;
    for (const TreeEntry entry : tree.subtree(fixture.root))
        subtree.push_back(entry);

    REQUIRE(subtree.size() == 3);
    REQUIRE(subtree[0].id == fixture.root);
    REQUIRE(subtree[0].depth == 0);
    REQUIRE(subtree[1].id == branch);
    REQUIRE(subtree[1].depth == 1);
    REQUIRE(subtree[2].id == children.front());
    REQUIRE(subtree[2].depth == 2);
}

TEST(TreeView_NodeMetadata, RuntimeFixture,
    "TreeView должен различать объявленный API слота и выбранную реализацию.") {
    auto& nodes = fixture.run_ctx.nodes;

    auto slot = fixture.build(fixture.root).addSlot<TestAPI>("selected");
    choose<+[](::NodeBuild& branch) { return branch.construct<TestImplA, TestAPI>(); }>(slot, "Tree::ImplA");

    const TreeNodeInfo info = fixture.build(fixture.root).tree().info(slot.id());
    REQUIRE(info.kind == NodeKind::Slot);
    REQUIRE(info.name == "selected");
    REQUIRE(info.type == "TestAPI");
    REQUIRE(info.implementation == "ImplA");
    REQUIRE(info.blueprint == nodes.blueprints.id<TestAPI>());
    REQUIRE(info.implementationBlueprint == nodes.blueprints.find("Tree::ImplA"));
    REQUIRE(info.hasObject);
    REQUIRE(slot.get() != nullptr);
}

TEST(Node_GlobalCollectDeepTree, RuntimeFixture,
    "globalCollect должен найти каждый компонент во всём дереве независимо от глубины и ветки.")
{
    auto& nodes = fixture.run_ctx.nodes;

    fixture.build(fixture.root).emplace<TestComponent>("root");

    const NodeId branchA = nodes.factory.folder(fixture.root, "A");
    fixture.build(branchA).emplace<TestComponent>("a");

    const NodeId branchB = nodes.factory.folder(fixture.root, "B");
    fixture.build(branchB).emplace<TestComponent>("b");

    const NodeId childA = nodes.factory.folder(branchA, "ChildA");
    fixture.build(childA).emplace<TestComponent>("aa");

    const NodeId childB = nodes.factory.folder(branchB, "ChildB");
    fixture.build(childB).emplace<TestComponent>("bb");

    const NodeId deep = nodes.factory.folder(childA, "Deep");
    fixture.build(deep).emplace<TestComponent>("aaa");

    auto rootComponent = fixture.build(fixture.root).require<TestComponent>("root");
    auto a = fixture.build(branchA).require<TestComponent>("a");
    auto b = fixture.build(branchB).require<TestComponent>("b");
    auto aa = fixture.build(childA).require<TestComponent>("aa");
    auto bb = fixture.build(childB).require<TestComponent>("bb");
    auto aaa = fixture.build(deep).require<TestComponent>("aaa");

    auto result = nodes.query.collect(nodes.ops.root(deep), typeKey<TestComponent>());

    REQUIRE(result.size() == 6);

    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == rootComponent.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == a.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == b.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == aa.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == bb.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == aaa.get(); }));
}

TEST(Node_GlobalCollectDifferentInstances, RuntimeFixture,
    "globalCollect должен возвращать все экземпляры одного типа независимо от их имён.")
{
    auto& nodes = fixture.run_ctx.nodes;

    fixture.build(fixture.root).emplace<TestComponent>("one");
    fixture.build(fixture.root).emplace<TestComponent>("two");
    fixture.build(fixture.root).emplace<TestComponent>("three");

    const NodeId branch = nodes.factory.folder(fixture.root, "branch");

    fixture.build(branch).emplace<TestComponent>("four");
    fixture.build(branch).emplace<TestComponent>("five");

    auto one = fixture.build(fixture.root).require<TestComponent>("one");
    auto two = fixture.build(fixture.root).require<TestComponent>("two");
    auto three = fixture.build(fixture.root).require<TestComponent>("three");
    auto four = fixture.build(branch).require<TestComponent>("four");
    auto five = fixture.build(branch).require<TestComponent>("five");

    auto result = nodes.query.collect(nodes.ops.root(fixture.root), typeKey<TestComponent>());

    REQUIRE(result.size() == 5);

    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == one.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == two.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == three.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == four.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == five.get(); }));
}

TEST(Node_GlobalCollectSameNames, RuntimeFixture,
    "globalCollect должен различать объекты с одинаковыми именами в разных ветках.")
{
    auto& nodes = fixture.run_ctx.nodes;

    const NodeId branchA = nodes.factory.folder(fixture.root, "A");
    const NodeId branchB = nodes.factory.folder(fixture.root, "B");

    fixture.build(branchA).emplace<TestComponent>("shared");
    fixture.build(branchB).emplace<TestComponent>("shared");

    auto a = fixture.build(branchA).require<TestComponent>("shared");
    auto b = fixture.build(branchB).require<TestComponent>("shared");

    auto result = nodes.query.collect(nodes.ops.root(fixture.root), typeKey<TestComponent>());

    REQUIRE(result.size() == 2);

    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == a.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == b.get(); }));

    REQUIRE(a.get() != b.get());
}

TEST(Node_GlobalCollectFromDeepNode, RuntimeFixture,
    "globalCollect должен искать от корня независимо от того, из какого узла он вызван.")
{
    auto& nodes = fixture.run_ctx.nodes;

    fixture.build(fixture.root).emplace<TestComponent>("root");

    const NodeId branchA = nodes.factory.folder(fixture.root, "A");
    fixture.build(branchA).emplace<TestComponent>("a");

    const NodeId branchB = nodes.factory.folder(fixture.root, "B");
    fixture.build(branchB).emplace<TestComponent>("b");

    const NodeId deep = nodes.factory.folder(branchA, "Deep");
    fixture.build(deep).emplace<TestComponent>("deep");

    auto rootComponent = fixture.build(fixture.root).require<TestComponent>("root");
    auto a = fixture.build(branchA).require<TestComponent>("a");
    auto b = fixture.build(branchB).require<TestComponent>("b");
    auto deepComponent = fixture.build(deep).require<TestComponent>("deep");

    auto result = nodes.query.collect(nodes.ops.root(deep), typeKey<TestComponent>());

    REQUIRE(result.size() == 4);

    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == rootComponent.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == a.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == b.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestComponent>()) == deepComponent.get(); }));
}

TEST(Node_GlobalCollectByRole, RuntimeFixture,
    "globalCollect должен находить все объекты, реализующие интерфейс, независимо от concrete-типа.")
{
    auto& nodes = fixture.run_ctx.nodes;

    fixture.build(fixture.root).emplace<TestImplA, TestAPI>("a");
    fixture.build(fixture.root).emplace<TestImplB, TestAPI>("b");

    const NodeId branch = nodes.factory.folder(fixture.root, "branch");
    fixture.build(branch).emplace<TestImplA, TestAPI>("c");

    auto a = fixture.build(fixture.root).require<TestAPI>("a");
    auto b = fixture.build(fixture.root).require<TestAPI>("b");
    auto c = fixture.build(branch).require<TestAPI>("c");

    auto result = nodes.query.collect(nodes.ops.root(fixture.root), typeKey<TestAPI>());

    REQUIRE(result.size() == 3);

    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestAPI>()) == a.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestAPI>()) == b.get(); }));
    REQUIRE(std::ranges::any_of(result, [&](NodeId id) { return nodes.query.resolve(id, typeKey<TestAPI>()) == c.get(); }));
}

TEST(Node_GlobalCollectIgnoresInstanceName, RuntimeFixture, 
"Глобальный поиск должен находить все экземпляры компонента независимо от имени реализации.")
 {
    auto& nodes = fixture.run_ctx.nodes;

    fixture.build(fixture.root).emplace<TestComponent>("one");
    fixture.build(fixture.root).emplace<TestComponent>("two");
    fixture.build(fixture.root).emplace<TestComponent>("three");

    const NodeId branch = nodes.factory.folder(fixture.root, "branch");
    fixture.build(branch).emplace<TestComponent>("four");
    fixture.build(branch).emplace<TestComponent>("five");

    auto Node = nodes.query.collect(nodes.ops.root(fixture.root), typeKey<TestComponent>());

    REQUIRE(Node.size() == 5);
}

TEST(Node_RemoveShadowDoesNotRevealWrongComponent, RuntimeFixture, 
"После удаления локального компонента поиск должен корректно продолжить поиск у родителя. \
Удаление индекса дочернего компонента не должно повреждать или скрывать родительский объект.")
{
    auto& nodes = fixture.run_ctx.nodes;

    fixture.build(fixture.root).emplace<TestComponent>("shared");

    const NodeId branch = nodes.factory.folder(fixture.root, "branch");
    fixture.build(branch).emplace<TestComponent>("shared");

    REQUIRE(fixture.build(branch).find<TestComponent>("shared").exists());

    nodes.ops.destroyBranch(nodes.query.find(branch, typeKey<TestComponent>(), "shared"));

    REQUIRE(fixture.build(branch).find<TestComponent>("shared").exists());
}

}
