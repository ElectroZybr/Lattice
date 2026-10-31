#include <string>

#include <Lattice/tests/RuntimeFixture.hpp>
#include <lattice-test/Tests.hpp>

#include <Lattice/Lattice.hpp>

namespace Lattice::ChoicesTests {

struct Device {
    virtual ~Device() = default;
    virtual int kind() const = 0;
};
struct Cpu final : Device { int kind() const override { return 1; } };
struct Gpu final : Device { int kind() const override { return 2; } };
struct Probe { int changes = 0; };

std::string text(Exports& exports, ExportId id) { return exports.value(id).get<std::string>(); }

TEST(Choices_FocusIsAParam, RuntimeFixture,
    "Точка фокуса — параметр выбора `device` на владельце scope: чтение и запись идут в Context. Общие кандидаты сами параметра не заводят.") {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    root.emplaceShared<Cpu, Device>("cpu");
    auto gpu = root.emplaceShared<Gpu, Device>("gpu");
    const auto role = nodes.context.role(typeKey<Device>());
    REQUIRE(nodes.exports.find(fixture.root, "device") == InvalidExportId);
    root.focus<Device>();
    const ExportId device = nodes.exports.find(fixture.root, "device");
    REQUIRE(device != InvalidExportId);
    REQUIRE(nodes.exports.find(fixture.root, "cpu") == InvalidExportId);
    REQUIRE(text(nodes.exports, device).empty());
    REQUIRE(nodes.exports.rules(device).choices.size() == 2);

    REQUIRE(nodes.exports.set(device, std::string{"gpu"}));
    REQUIRE(nodes.context.resolve(nodes.context.root(), role) == gpu.id());
    REQUIRE(text(nodes.exports, device) == "gpu");

    const auto refused = nodes.exports.set(device, std::string{"tpu"});
    REQUIRE(!refused && refused.rejection().code == ParamError::Choice);
    REQUIRE(refused.rejection().expected.size() == 2);
    REQUIRE(text(nodes.exports, device) == "gpu");

    REQUIRE(nodes.exports.set(device, std::string{}));
    REQUIRE(nodes.context.resolveInfo(nodes.context.root(), role).state == ContextResolutionState::Ambiguous);
}

TEST(Choices_OnlyCandidateIsPinnedOnUse, RuntimeFixture,
    "Единственный кандидат закрепляется при первой нужде: второй его не меняет, уход освобождает.") {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    auto first = root.branch("first");
    first.emplaceShared<Cpu, Device>("cpu");
    REQUIRE(nodes.exports.find(fixture.root, "device") == InvalidExportId);

    auto user = root.branch("user");
    auto focus = user.focus<Device>();
    REQUIRE(focus->kind() == 1);
    root.emplaceShared<Gpu, Device>("gpu");
    REQUIRE(focus->kind() == 1);
    REQUIRE(nodes.exports.find(fixture.root, "device") == InvalidExportId);

    nodes.ops.destroyBranch(first.id());
    REQUIRE(focus->kind() == 2);
}

TEST(Choices_LocalFocusPoint, RuntimeFixture,
    "focus<T>() в ветке — свой параметр выбора: перекрывает родительский только для ветки.") {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    root.emplaceShared<Cpu, Device>("cpu");
    root.emplaceShared<Gpu, Device>("gpu");
    auto fast = root.branch("fast");
    auto local = fast.focus<Device>();
    const ExportId fastDevice = nodes.exports.find(fast.id(), "device");
    REQUIRE(fastDevice != InvalidExportId);

    REQUIRE(nodes.exports.set(fastDevice, std::string{"gpu"}));
    REQUIRE(local->kind() == 2);
    REQUIRE(nodes.exports.find(fixture.root, "device") == InvalidExportId);
}

TEST(Choices_WriteWakesObservers, RuntimeFixture,
    "Запись выбора — изменение Context: onChange срабатывает в maintain.") {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    root.emplaceShared<Cpu, Device>("cpu");
    root.emplaceShared<Gpu, Device>("gpu");
    auto probe = root.emplace<Probe>("probe");
    auto focus = root.focus<Device>();
    focus.onChange(probe.ref(), [](Probe& owner, const Focus<Device>&) { ++owner.changes; });

    REQUIRE(nodes.exports.set(nodes.exports.find(fixture.root, "device"), std::string{"cpu"}));
    nodes.ops.maintain();
    REQUIRE(probe->changes == 1 && focus->kind() == 1);
}

TEST(Choices_NameTakenFallsBackToRoleKey, RuntimeFixture,
    "Имя `device` занято экспортом владельца — выбор называется `deviceFocus`.") {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    int device = 0;
    root.param("device", device);
    root.emplaceShared<Cpu, Device>("cpu");
    root.emplaceShared<Gpu, Device>("gpu");
    root.focus<Device>();
    REQUIRE(nodes.exports.find(fixture.root, "deviceFocus") != InvalidExportId);
}


struct Renderer {
    virtual ~Renderer() = default;
    virtual int backend() const = 0;
};
struct Software final : Renderer { int backend() const override { return 1; } };
struct Hardware final : Renderer { int backend() const override { return 2; } };
auto composeSoftware(NodeBuild& slot) { return slot.construct<Software, Renderer>(); }
auto composeHardware(NodeBuild& slot) { return slot.construct<Hardware, Renderer>(); }

void composeRenderer(NodeBuild root) { root.addSlot<Renderer>("renderer"); }

TEST(Choices_ImplementationBuildsAChild, RuntimeFixture,
    "Builder собирает реализацию из каталога обычным ребёнком: объект под API, его тип — как у ребёнка из compose.") {
    auto& nodes = fixture.run_ctx.nodes;
    const auto hardware = nodes.blueprints.addImplementation(implementation<Renderer, composeHardware>("Hardware"), "Render");
    const auto child = nodes.builder.add(fixture.root, hardware, "main");
    const auto& node = nodes.registry.require(child);
    REQUIRE(node.kind != NodeKind::Slot && node.bp == nodes.blueprints.id<Renderer>() && node.choice == hardware,
            "a node of the API, as a slot holds it; the implementation is its choice");
    REQUIRE(static_cast<const Renderer*>(nodes.factory.resolve(child, nodes.blueprints.id<Renderer>()))->backend() == 2);
    REQUIRE(node.object.bp == nodes.blueprints.id<Hardware>(), "the object is the implementation's type");
    REQUIRE(nodes.exports.find(fixture.root, "main") == InvalidExportId, "a child, not a slot: no choice param");
}

struct Lamp { int watts = 60; };
auto composeLamp(NodeBuild& node) { return node.construct<Lamp>(); }

TEST(Choices_ConcreteImplementationBuildsAChild, RuntimeFixture,
    "Реализация, чей API — сам конкретный тип, собирается и тогда, когда тип зарегистрирован раньше объекта (как API каталога).") {
    auto& nodes = fixture.run_ctx.nodes;
    const auto lamp = nodes.blueprints.addImplementation(implementation<Lamp, composeLamp>("Lamp"), "Scene");
    const auto child = nodes.builder.add(fixture.root, lamp, "desk");
    REQUIRE(static_cast<const Lamp*>(nodes.factory.resolve(child, nodes.blueprints.id<Lamp>()))->watts == 60);
    nodes.builder.add(fixture.root, lamp, "floor");
    REQUIRE(nodes.exports.find(fixture.root, "lamp") == InvalidExportId, "two parts of one type are not a choice");
}

TEST(Choices_ImplementationChildIsLikeEmplaced, RuntimeFixture,
    "Узел из реализации, собранный в любое время, — такой же, как emplace в compose: тип, имя, поиск, configure.") {
    auto& nodes = fixture.run_ctx.nodes;
    static int configured;
    configured = 0;
    const auto lamp = nodes.blueprints.addImplementation(
        implementation<Lamp, composeLamp>("Lamp", +[](NodeConfigure) { ++configured; }), "Room");
    fixture.build(fixture.root).emplace<Lamp>("desk");
    const auto floor = nodes.builder.add(fixture.root, lamp, "floor");
    const TreeView tree{nodes};
    const auto desk = nodes.registry.find("desk", fixture.root, nodes.blueprints.id<Lamp>());
    REQUIRE(desk != InvalidNodeId && nodes.registry.find("floor", fixture.root, nodes.blueprints.id<Lamp>()) == floor,
            "both are found by their type");
    REQUIRE(tree.info(desk).type == "Lamp" && tree.info(floor).type == "Lamp", "one type in the tree");
    REQUIRE(tree.info(floor).implementation == "Lamp" && configured == 1, "the implementation stays known and configures it");
    bool refused = false;
    try { nodes.builder.add(fixture.root, lamp, "desk"); } catch (const ExceptionBase&) { refused = true; }
    REQUIRE(refused, "a name taken by an emplaced node of the type is taken for the implementation too");
}

TEST(Choices_ImplementationComposedInCompose, RuntimeFixture,
    "Реализация каталога собирается и внутри compose (`root.compose`) — тем же узлом, что Builder::add позже.") {
    auto& nodes = fixture.run_ctx.nodes;
    const auto lamp = nodes.blueprints.addImplementation(implementation<Lamp, composeLamp>("Lamp"), "Hall");
    const auto room = nodes.blueprints.addComposition("Hall", +[](NodeBuild root) { root.compose("Hall::Lamp", "desk"); });
    const auto branch = nodes.builder.add(fixture.root, room, "hall");
    const auto desk = nodes.registry.find("desk", branch, nodes.blueprints.id<Lamp>());
    REQUIRE(desk != InvalidNodeId && nodes.registry.require(desk).choice == lamp, "a node of the type, built by the implementation");
    const auto floor = nodes.builder.add(branch, lamp, "floor");
    REQUIRE(nodes.registry.require(floor).bp == nodes.registry.require(desk).bp, "the same as one added later");
}

TEST(Choices_SlotWithOneImplementationFillsItself, RuntimeFixture,
    "Одна реализация API заполняет слот в конце композиции; новая реализация выбор не меняет.") {
    auto& nodes = fixture.run_ctx.nodes;
    nodes.blueprints.addImplementation(implementation<Renderer, composeSoftware>("Software"), "Render");
    const auto branch = nodes.builder.add(fixture.root, nodes.blueprints.addComposition("Scene", composeRenderer), "scene");
    auto slot = NodeBuild{branch, nodes}.addSlot<Renderer>("renderer");
    REQUIRE(slot && slot->backend() == 1);
    const ExportId renderer = nodes.exports.find(branch, "renderer");
    REQUIRE(renderer != InvalidExportId && text(nodes.exports, renderer) == "Software");

    nodes.blueprints.addImplementation(implementation<Renderer, composeHardware>("Hardware"), "Render");
    nodes.ops.maintain();
    REQUIRE(slot->backend() == 1);
    REQUIRE(nodes.exports.rules(renderer).choices.size() == 2);
}

TEST(Choices_SlotWithSeveralWaitsForAChoice, RuntimeFixture,
    "Две реализации — слот пуст до выбора; выбор и замена — параметром, чужое имя — отказ.") {
    auto& nodes = fixture.run_ctx.nodes;
    nodes.blueprints.addImplementation(implementation<Renderer, composeSoftware>("Software"), "Render");
    nodes.blueprints.addImplementation(implementation<Renderer, composeHardware>("Hardware"), "Render");
    const auto branch = nodes.builder.add(fixture.root, nodes.blueprints.addComposition("Scene", composeRenderer), "scene");
    auto slot = NodeBuild{branch, nodes}.addSlot<Renderer>("renderer");
    nodes.ops.maintain();
    REQUIRE(!slot.exists());
    const ExportId renderer = nodes.exports.find(branch, "renderer");
    REQUIRE(text(nodes.exports, renderer).empty());
    bool refused = false;
    try { slot.ref(); } catch (const ExceptionBase&) { refused = true; }
    REQUIRE(refused);

    const auto unknown = nodes.exports.set(renderer, std::string{"Vulkan"});
    REQUIRE(!unknown && unknown.rejection().code == ParamError::Choice && unknown.rejection().expected.size() == 2);

    REQUIRE(nodes.exports.set(renderer, std::string{"Hardware"}));
    REQUIRE(slot->backend() == 2);
    REQUIRE(nodes.exports.set(renderer, std::string{"Render::Software"}));
    REQUIRE(text(nodes.exports, renderer) == "Software");
    nodes.ops.maintain();
    REQUIRE(slot->backend() == 1);
    REQUIRE(nodes.exports.find(branch, "renderer") == renderer);
}


struct Viewer {
    explicit Viewer(Renderer& renderer) : renderer(renderer) {}
    Renderer& renderer;
};
struct DeviceUser {
    explicit DeviceUser(Device& device) : device(device) {}
    Device& device;
};

void composeViewer(NodeBuild root) {
    auto slot = root.addSlot<Renderer>("renderer");
    root.emplace<Viewer>("viewer", slot.ref());
}
void composeNestedViewer(NodeBuild root) {
    auto view = root.branch("view");
    auto slot = view.addSlot<Renderer>("renderer");
    view.emplace<Viewer>("viewer", slot.ref());
}
void composeDeviceUser(NodeBuild root) { root.emplace<DeviceUser>("user", root.focus<Device>().ref()); }

TEST(Choices_PendingBranchWaitsForItsSlot, RuntimeFixture,
    "Нужен невыбранный слот — ветка ждёт: заместитель выбора на ней, после записи ветка собирается.") {
    auto& nodes = fixture.run_ctx.nodes;
    nodes.blueprints.addImplementation(implementation<Renderer, composeSoftware>("Software"), "Render");
    nodes.blueprints.addImplementation(implementation<Renderer, composeHardware>("Hardware"), "Render");
    const auto branch = nodes.builder.add(fixture.root, nodes.blueprints.addComposition("Scene", composeViewer), "scene");
    REQUIRE(nodes.registry.children(branch).empty());
    const auto pending = NodeBuild{fixture.root, nodes}.exports().pending();
    REQUIRE(pending.size() == 1 && pending.front().branch == branch);
    REQUIRE(pending.front().name == "renderer" && pending.front().options.size() == 2);

    std::vector<ParamChange> changes;
    const auto observer = nodes.exports.observeParameters([&](ExportId, ParamChange change) { changes.push_back(change); });
    REQUIRE(!nodes.exports.set(pending.front().param, std::string{"Vulkan"}));
    REQUIRE(nodes.exports.set(pending.front().param, std::string{"Hardware"}));
    nodes.ops.maintain();
    auto viewer = NodeBuild{branch, nodes}.require<Viewer>("viewer");
    REQUIRE(viewer->renderer.backend() == 2);
    REQUIRE(NodeBuild(fixture.root, nodes).exports().pending().empty());
    const ExportId renderer = nodes.exports.find(branch, "renderer");
    REQUIRE(renderer != InvalidExportId && renderer != pending.front().param);
    REQUIRE(text(nodes.exports, renderer) == "Hardware");
    REQUIRE(std::ranges::count(changes, ParamChange::Set) == 2);
    nodes.exports.unobserveParameters(observer);
}

TEST(Choices_PendingNestedChoiceIsNamedByPath, RuntimeFixture,
    "Выбор внутри вложенного узла ветки — заместитель `view.renderer`; выбор доходит до слота.") {
    auto& nodes = fixture.run_ctx.nodes;
    nodes.blueprints.addImplementation(implementation<Renderer, composeSoftware>("Software"), "Render");
    nodes.blueprints.addImplementation(implementation<Renderer, composeHardware>("Hardware"), "Render");
    const auto branch = nodes.builder.add(fixture.root, nodes.blueprints.addComposition("Scene", composeNestedViewer), "scene");
    const ExportId placeholder = nodes.exports.find(branch, "view.renderer");
    REQUIRE(placeholder != InvalidExportId);
    REQUIRE(nodes.exports.set(placeholder, std::string{"Software"}));
    nodes.ops.maintain();
    const auto view = nodes.registry.find("view", branch);
    REQUIRE(view != InvalidNodeId);
    REQUIRE(NodeBuild(view, nodes).require<Viewer>("viewer")->renderer.backend() == 1);
    REQUIRE(nodes.exports.find(branch, "view.renderer") == InvalidExportId);
}

TEST(Choices_PendingBranchWaitsForAnOuterFocus, RuntimeFixture,
    "Неоднозначная внешняя роль — ветка ждёт выбора `device` снаружи; его запись собирает ветку.") {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    root.emplaceShared<Cpu, Device>("cpu");
    root.emplaceShared<Gpu, Device>("gpu");
    const auto branch = nodes.builder.add(fixture.root, nodes.blueprints.addComposition("User", composeDeviceUser), "user");
    const auto pending = root.exports().pending();
    REQUIRE(pending.size() == 1 && pending.front().param == nodes.exports.find(fixture.root, "device"));
    REQUIRE(nodes.registry.children(branch).empty());

    REQUIRE(nodes.exports.set(pending.front().param, std::string{"gpu"}));
    nodes.ops.maintain();
    REQUIRE(NodeBuild(branch, nodes).require<DeviceUser>("user")->device.kind() == 2);
    REQUIRE(root.exports().pending().empty());
}

TEST(Choices_UnneededAmbiguityIsNoError, RuntimeFixture,
    "Невыбранное, к которому никто не обратился, ветку не останавливает.") {
    auto& nodes = fixture.run_ctx.nodes;
    nodes.blueprints.addImplementation(implementation<Renderer, composeSoftware>("Software"), "Render");
    nodes.blueprints.addImplementation(implementation<Renderer, composeHardware>("Hardware"), "Render");
    const auto branch = nodes.builder.add(fixture.root, nodes.blueprints.addComposition("Scene", composeRenderer), "scene");
    REQUIRE(nodes.registry.children(branch).size() == 1);
    REQUIRE(NodeBuild(fixture.root, nodes).exports().pending().empty());
}


struct Owned { float value = 0.0f; };
struct Queue {
    template<class F> void call(F&& work) { work(); }
};
void composeOwnedSlot(NodeBuild root) {
    auto queue = root.emplace<Queue>("queue");
    root.addSlot<Renderer>("renderer");          // its choice param lands on this node first
    auto owned = root.construct<Owned>();
    owned.executeOn(queue);
    owned.param("value", &Owned::value);
}

TEST(Choices_ParamsOfTheKernelLetExecuteOnFollow, RuntimeFixture,
    "Параметр выбора слота на узле не мешает объекту узла объявить executeOn позже.") {
    auto& nodes = fixture.run_ctx.nodes;
    nodes.blueprints.addImplementation(implementation<Renderer, composeSoftware>("Software"), "Render");
    const auto branch = nodes.builder.add(fixture.root, nodes.blueprints.addComposition("Owner", composeOwnedSlot), "owner");
    REQUIRE(nodes.exports.find(branch, "renderer") != InvalidExportId);
    REQUIRE(nodes.exports.find(branch, "value") != InvalidExportId);
}

}
