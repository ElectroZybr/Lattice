#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/tools/Tests.hpp>
#include <Lattice/Lattice.hpp>

namespace Lattice {

struct SlotAPI {
    virtual ~SlotAPI() = default;
    virtual int id() const = 0;
};

struct SlotImplA : SlotAPI {
    int id() const override { return 1; }
};

struct SlotImplB : SlotAPI {
    int id() const override { return 2; }
};

struct SlotInput {};

struct SlotWindowImpl : SlotAPI {
    int id() const override { return 7; }
};

namespace {

struct RollbackChild {
    static inline int alive = 0;
    RollbackChild() { ++alive; }
    ~RollbackChild() { --alive; }
};

ObjectBuild<SlotImplA> composeA(::NodeBuild& branch) {
    return branch.construct<SlotImplA, SlotAPI>();
}

ObjectBuild<SlotImplB> composeB(::NodeBuild& branch) {
    return branch.construct<SlotImplB, SlotAPI>();
}

ObjectBuild<SlotWindowImpl> composeWindow(::NodeBuild& branch) {
    branch.emplace<SlotInput>("input");
    return branch.construct<SlotWindowImpl, SlotAPI>();
}

ObjectBuild<SlotImplA> composeThrowing(::NodeBuild& branch) {
    branch.emplace<RollbackChild>("child");
    throw Exception<SlotAPI>("Composition failed");
}

template<class F> bool slotThrows(F&& operation) {
    try { operation(); } catch (const ExceptionBase&) { return true; }
    return false;
}

}

TEST(Slot_EmptyNode, RuntimeFixture,
    "Пустой слот должен существовать как нода без runtime object.")
{
    auto slot = fixture.build(fixture.root).addSlot<SlotAPI>();
    const auto& node = fixture.run_ctx.nodes.registry.require(slot.id());

    REQUIRE(node.kind == NodeKind::Slot);
    REQUIRE(node.bp == fixture.run_ctx.blueprints.id<SlotAPI>());
    REQUIRE(node.object.ptr == nullptr);
    REQUIRE(node.object.bp == InvalidBlueprintId);
    REQUIRE(!slot.exists());
}

TEST(Slot_ChoiceReplacesImpl, RuntimeFixture,
    "Повторный choice завершает старое содержимое slot и после maintain собирает новую реализацию.")
{
    auto& nodes = fixture.run_ctx.nodes;
    auto slot = fixture.build(fixture.root).addSlot<SlotAPI>();

    REQUIRE(choose<composeWindow>(slot, "Slot::Window"));
    REQUIRE(slot->id() == 7);
    REQUIRE(nodes.query.collect(slot.id(), nodes.blueprints.id<SlotInput>()).size() == 1);

    REQUIRE(choose<composeB>(slot, "Slot::B"));

    REQUIRE(slot->id() == 2);
    REQUIRE(nodes.query.collect(slot.id(), nodes.blueprints.id<SlotInput>()).empty());
}

TEST(Slot_RejectsWrongImpl, RuntimeFixture,
    "Slot не должен принимать blueprint, который не реализует его API.")
{
    auto slot = fixture.build(fixture.root).addSlot<SlotAPI>();
    const auto unrelated = fixture.run_ctx.blueprints.addComposition("Slot::Unrelated", +[](::NodeBuild) {});

    REQUIRE(unrelated != InvalidBlueprintId);
    REQUIRE(slotThrows([&] { slot.select("Slot::Unrelated"); }));
    REQUIRE(!slot.exists());
}

TEST(Slot_ChoiceSameImpl, RuntimeFixture,
    "Повторный выбор уже установленной реализации не должен пересоздавать объект.")
{
    auto slot = fixture.build(fixture.root).addSlot<SlotAPI>();

    choose<composeA>(slot, "Slot::A");
    SlotAPI* first = slot.get();
    REQUIRE(choose<composeA>(slot, "Slot::A"));

    REQUIRE(slot.get() == first);
}

TEST(Slot_CreatesContextRole, RuntimeFixture,
    "Создание slot должно сразу зарегистрировать его как target соответствующей роли.")
{
    auto slot = fixture.build(fixture.root).addSlot<SlotAPI>();

    const ContextScopeId scope = fixture.run_ctx.nodes.context.findScope(fixture.root);
    const RoleId role = fixture.run_ctx.nodes.context.findRole(typeKey<SlotAPI>());

    REQUIRE(scope != InvalidContextScopeId);
    REQUIRE(role != InvalidRoleId);
    REQUIRE(fixture.run_ctx.nodes.context.resolve(scope, role) == slot.id());
}

TEST(Slot_FocusTracksImplementation, RuntimeFixture,
    "Focus на роль slot должен видеть текущую реализацию после choice.")
{
    auto slot = fixture.build(fixture.root).addSlot<SlotAPI>();
    auto focus = fixture.build(fixture.root).focus<SlotAPI>();

    REQUIRE(!focus);

    choose<composeA>(slot, "Slot::A");

    REQUIRE(focus.id() == slot.id());
    REQUIRE(focus->id() == 1);

    REQUIRE(choose<composeB>(slot, "Slot::B"));

    REQUIRE(focus.id() == slot.id());
    REQUIRE(focus->id() == 2);
}

TEST(Slot_RepeatedCreation, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto first = fixture.build(fixture.root).addSlot<SlotAPI>();
    choose<composeA>(first, "Slot::A");
    auto again = fixture.build(fixture.root).addSlot<SlotAPI>();
    REQUIRE(first.id() == again.id());
    REQUIRE(first.get() == again.get());
    REQUIRE(nodes.registry.children(fixture.root).size() == 1);
}

TEST(Slot_AbstractChoicePreservesContents, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto slot = fixture.build(fixture.root).addSlot<SlotAPI>();
    choose<composeWindow>(slot, "Slot::Window");
    auto* previous = slot.get();
    REQUIRE(slotThrows([&] { slot.select(typeKey<SlotAPI>()); }));
    REQUIRE(slot.get() == previous);
    REQUIRE(nodes.registry.children(slot.id()).size() == 1);
}

TEST(Slot_CompositionFailureClearsChildren, RuntimeFixture) {
    auto& nodes = fixture.run_ctx.nodes;
    auto slot = fixture.build(fixture.root).addSlot<SlotAPI>();
    auto focus = fixture.build(fixture.root).focus<SlotAPI>();
    REQUIRE(slotThrows([&] { choose<composeThrowing>(slot, "Slot::Throwing"); }));
    REQUIRE(!slot);
    REQUIRE(RollbackChild::alive == 0);
    REQUIRE(nodes.registry.children(slot.id()).empty());
    REQUIRE(focus.id() == slot.id());
    REQUIRE(!focus);
    choose<composeB>(slot, "Slot::B");
    REQUIRE(focus->id() == 2);
}

}
