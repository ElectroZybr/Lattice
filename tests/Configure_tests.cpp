#include <string>
#include <vector>
#include <Lattice/Composition.hpp>
#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/Tools/Tests.hpp>

namespace Lattice::ConfigureTests {
namespace {

struct Item {
    explicit Item(int value = 0) : value(value) {}
    int value;
};

// Shared choice of the environment, separate from the drawn items: same-type
// siblings are Context candidates, so items themselves would be ambiguous.
struct Marker {
    explicit Marker(int value = 0) : value(value) {}
    int value;
};

// Ordinary class: the set it draws is a plain vector assigned by configure.
struct View {
    std::vector<Item*> items;
    Marker* selected = nullptr;
    Marker* environment = nullptr;
    int configured = 0;
};

void composeScene(NodeBuild root) {
    root.emplace<Item>("a", 1);
    root.construct<View>();
}

void configureScene(NodeConfigure root) {
    auto* view = root.self<View>();
    view->items = root.children<Item>();
    view->selected = root.focus<Marker>();
    view->environment = root.find<Marker>("shared");
    ++view->configured;
}

struct Scene {
    NodeId branch;
    View* view;
};

Scene addScene(RuntimeFixture& fixture, std::string_view name = "scene") {
    auto& blueprints = fixture.run_ctx.blueprints;
    auto type = blueprints.find("ConfigureScene");
    if (type == InvalidBlueprintId)
        type = blueprints.addComposition("ConfigureScene", composeScene, configureScene);
    const auto branch = fixture.run_ctx.nodes.builder.add(fixture.root, type, name);
    return {branch, static_cast<View*>(fixture.run_ctx.nodes.registry.require(branch).object.ptr)};
}

}

TEST(Configure_RunsAfterCompose, RuntimeFixture,
    "configure пакета вызывается сразу после compose и видит детей ветки.")
{
    const auto scene = addScene(fixture);
    REQUIRE(scene.view->configured == 1);
    REQUIRE(scene.view->items.size() == 1 && scene.view->items[0]->value == 1);
}

TEST(Configure_FollowsAddedChildren, RuntimeFixture,
    "Новый ребёнок ветки попадает в набор после maintain, без пересборки объекта.")
{
    auto& nodes = fixture.run_ctx.nodes;
    const auto scene = addScene(fixture);
    fixture.build(scene.branch).emplace<Item>("b", 2);
    nodes.ops.maintain();
    REQUIRE(scene.view->items.size() == 2);
    nodes.ops.maintain();
    REQUIRE(scene.view->configured == 2);
}

TEST(Configure_RetiringChildLeavesBeforeDestruction, RuntimeFixture,
    "Retire ребёнка не завершает потребителя: он исчезает из набора ещё до удаления.")
{
    auto& nodes = fixture.run_ctx.nodes;
    const auto scene = addScene(fixture);
    const auto b = fixture.build(scene.branch).emplace<Item>("b", 2);
    static bool ready;
    ready = false;
    // A failed check must not leave the fixture waiting for this node forever.
    struct Release { ~Release() { ready = true; } } release;
    nodes.ops.readyToDestroy(b.id(), [](const void*) { return ready; });
    nodes.ops.maintain();
    REQUIRE(scene.view->items.size() == 2);

    nodes.ops.retireBranch(b.id());
    nodes.ops.maintain();
    REQUIRE(nodes.registry.get(b.id()) != nullptr);
    REQUIRE(scene.view->items.size() == 1 && scene.view->items[0]->value == 1);
    REQUIRE(nodes.registry.require(scene.branch).state == NodeState::Active);

    ready = true;
    nodes.ops.maintain();
    REQUIRE(nodes.registry.get(b.id()) == nullptr);
    REQUIRE(scene.view->items.size() == 1);
}

TEST(Configure_DestroyOutsideMaintainRewiresImmediately, RuntimeFixture,
    "Удаление узла вне maintain сразу переконфигурирует ветку: висячих указателей нет.")
{
    auto& nodes = fixture.run_ctx.nodes;
    const auto scene = addScene(fixture);
    const auto b = fixture.build(scene.branch).emplace<Item>("b", 2);
    nodes.ops.maintain();
    nodes.ops.destroyBranch(b.id());
    REQUIRE(scene.view->items.size() == 1);
}

TEST(Configure_FocusChangeReconfigures, RuntimeFixture,
    "Смена Focus переконфигурирует ветку на следующем maintain.")
{
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    const auto first = root.emplaceShared<Marker>("first", 1);
    const auto scene = addScene(fixture);
    REQUIRE(scene.view->selected == first.get());

    const auto second = root.emplaceShared<Marker>("second", 2);
    root.focus<Marker>(second.ref());
    nodes.ops.maintain();
    REQUIRE(scene.view->selected == second.get());
}

TEST(Configure_AmbiguousFocusIsAnError, RuntimeFixture,
    "Неоднозначный Focus в configure — ошибка, а не первый кандидат.")
{
    auto root = fixture.build(fixture.root);
    root.emplaceShared<Marker>("first", 1);
    root.emplaceShared<Marker>("second", 2);
    bool thrown = false;
    try {
        addScene(fixture);
    } catch (const ExceptionBase&) {
        thrown = true;
    }
    REQUIRE(thrown);
}

TEST(Configure_ObservedLookupRetires, RuntimeFixture,
    "Найденный вне ветки объект при retire переконфигурирует наблюдателя.")
{
    auto& nodes = fixture.run_ctx.nodes;
    const auto shared = fixture.build(fixture.root).emplace<Marker>("shared", 7);
    const auto scene = addScene(fixture);
    REQUIRE(scene.view->environment == shared.get());

    nodes.ops.retireBranch(shared.id());
    nodes.ops.maintain();
    REQUIRE(scene.view->environment == nullptr);
    REQUIRE(nodes.registry.require(scene.branch).state == NodeState::Active);
}

TEST(Configure_NestedChangeWakesOuterBranch, RuntimeFixture,
    "Изменение во вложенной композиции будит configure внешней ветки.")
{
    auto& nodes = fixture.run_ctx.nodes;
    auto& blueprints = fixture.run_ctx.blueprints;
    blueprints.addComposition("ConfigureInner", +[](NodeBuild root) { root.emplace<Item>("x", 1); });
    const auto outer = blueprints.addComposition("ConfigureOuter",
        +[](NodeBuild root) {
            root.compose("ConfigureInner", "inner");
            root.construct<View>();
        },
        +[](NodeConfigure root) { root.self<View>()->items = root.collect<Item>(); });
    const auto branch = nodes.builder.add(fixture.root, outer, "outer");
    auto* view = static_cast<View*>(nodes.registry.require(branch).object.ptr);
    REQUIRE(view->items.size() == 1);

    const auto inner = nodes.registry.find("inner", branch, blueprints.find("ConfigureInner"));
    fixture.build(inner).emplace<Item>("y", 2);
    nodes.ops.maintain();
    REQUIRE(view->items.size() == 2);
}

namespace {
struct Holder {
    virtual ~Holder() = default;
    std::vector<Item*> items;
};
struct HolderImpl final : Holder {};
}

TEST(Configure_SlotVariantHasConfigure, RuntimeFixture,
    "Вариант слота передаёт configure в choice; он работает как у пакета.")
{
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    auto slot = root.addSlot<Holder>("holder");
    slot.choice("ConfigureTests::Holder",
        +[](NodeBuild& node) {
            node.emplace<Item>("one", 1);
            return node.construct<HolderImpl, Holder>();
        },
        +[](NodeConfigure node) { node.self<Holder>()->items = node.children<Item>(); });
    REQUIRE(slot->items.size() == 1);

    fixture.build(slot.id()).emplace<Item>("two", 2);
    nodes.ops.maintain();
    REQUIRE(slot->items.size() == 2);
}

TEST(Configure_FocusByObjectAndFocusId, RuntimeFixture,
    "Focus::set(T*) выбирает узел объекта, focusId в configure отдаёт его NodeId.")
{
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    auto branch = root.branch("markers");
    const auto first = branch.emplace<Marker>("first", 1);
    const auto second = branch.emplace<Marker>("second", 2);
    auto active = branch.focus<Marker>();
    active.set(second.get());
    REQUIRE(active.id() == second.id());

    Marker stranger{3};
    bool thrown = false;
    try { active.set(&stranger); } catch (const ExceptionBase&) { thrown = true; }
    REQUIRE(thrown && active.id() == second.id());

    const NodeConfigure view{branch.id(), nodes, InvalidNodeId};
    REQUIRE(view.focusId<Marker>() == second.id());
    active.set(first.get());
    REQUIRE(view.focusId<Marker>() == first.id());
}

}
