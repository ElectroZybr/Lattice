#include <memory>
#include <string>
#include <stdexcept>
#include <vector>
#include <Lattice/Composition.hpp>
#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/Tools/Tests.hpp>

namespace Lattice {
namespace {
std::vector<int> events;
struct Storage {
    Storage() { events.push_back(1); }
    ~Storage() { events.push_back(-1); }
};
struct Algorithm {
    Algorithm(Storage& storage, std::unique_ptr<int> value)
        : storage(storage), value(std::move(value)) { events.push_back(2); }
    ~Algorithm() { events.push_back(-2); }
    Storage& storage;
    std::unique_ptr<int> value;
};
struct Simulation {
    explicit Simulation(Algorithm& algorithm) : algorithm(algorithm) {}
    Simulation(const Simulation&) = delete;
    Simulation(Simulation&&) = delete;
    Algorithm& algorithm;
};
void compose(NodeBuild root) {
    auto storage = root.emplace<Storage>("storage");
    auto algorithm = root.emplace<Algorithm>("algorithm", storage, std::make_unique<int>(42));
    root.param("value", *algorithm->value);
    root.emplace<Simulation>("simulation", algorithm);
}
struct Throws {
    explicit Throws(int) { throw std::runtime_error("failed"); }
};
void composeFailure(NodeBuild root) {
    root.emplace<Storage>("storage");
    root.emplace<Throws>("failure", 1);
    throw std::runtime_error("unreachable");
}
}
TEST(Composition_DefaultBuild, RuntimeFixture) {
    events.clear();
    auto& types = fixture.run_ctx.blueprints;
    auto& nodes = fixture.run_ctx.nodes;
    auto type = types.addComposition("Example", compose);
    auto branch = nodes.builder.add(fixture.root, type, "default");
    auto simulation = fixture.build(branch).require<Simulation>("simulation");
    REQUIRE(*simulation->algorithm.value == 42);
    REQUIRE(nodes.registry.require(branch).object.ptr == nullptr);
    auto second = nodes.builder.add(fixture.root, type, "second");
    REQUIRE(fixture.build(second).require<Simulation>("simulation").get() != simulation.get());
    nodes.ops.destroyBranch(second);
    nodes.ops.destroyBranch(branch);
    REQUIRE((events == std::vector<int>{1, 2, 1, 2, -2, -1, -2, -1}));
}
TEST(Composition_Rollback, RuntimeFixture) {
    events.clear();
    auto& types = fixture.run_ctx.blueprints;
    auto& nodes = fixture.run_ctx.nodes;
    auto type = types.addComposition("FailingExample", composeFailure);
    bool failed = false;
    try { nodes.builder.add(fixture.root, type); }
    catch (const std::runtime_error&) { failed = true; }
    REQUIRE(failed);
    REQUIRE(nodes.registry.children(fixture.root).empty());
    REQUIRE((events == std::vector<int>{1, -1}));
}
namespace {
struct Number {
    explicit Number(int value) : value(value) {}
    int value;
};
struct Unavailable {};
struct NeedsNumber {
    explicit NeedsNumber(Number& number) : number(number) {}
    Number& number;
};
struct MaybeNumber {
    explicit MaybeNumber(Number* number) : number(number) {}
    Number* number;
};
struct NumberList {
    explicit NumberList(std::vector<Number*> values) : values(std::move(values)) {}
    std::vector<Number*> values;
};
struct ExportedNumber {
    explicit ExportedNumber(Number& source) : source(source) {}
    void step() { ++value; }
    Number& source;
    int value = 0;
};
struct NumberAPI { virtual ~NumberAPI() = default; virtual int value() const = 0; };
struct FirstNumber : NumberAPI { int value() const override { return 1; } };
struct SecondNumber : NumberAPI { int value() const override { return 2; } };
}

TEST(Composition_QueryArgumentsAndSnapshots, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto& nodes = fixture.run_ctx.nodes;
    auto one = root.emplace<Number>("one", 1);
    auto two = root.emplace<Number>("two", 2);
    auto nested = root.branch("nested");
    auto three = nested.emplace<Number>("three", 3);
    REQUIRE(root.children<Number>().size() == 2);
    REQUIRE(root.collect<Number>().size() == 3);
    REQUIRE(root.children<Unavailable>().empty());
    REQUIRE(root.collect<Unavailable>().empty());
    REQUIRE(!root.find<Unavailable>());

    auto direct = nested.emplace<NeedsNumber>("direct", one);
    auto required = nested.emplace<NeedsNumber>("required", root.require<Number>("one"));
    REQUIRE(&direct->number == &required->number);
    REQUIRE(nodes.dependencies.retained(direct.id(), one.id()));
    REQUIRE(nodes.dependencies.retained(required.id(), one.id()));
    // Merely querying does not register a dependency on the query scope.
    REQUIRE(nodes.dependencies.dependencies(fixture.root).empty());

    auto optional = root.emplace<MaybeNumber>("optional", root.find<Number>("missing"));
    auto present = root.emplace<MaybeNumber>("present", root.find<Number>("two"));
    REQUIRE(optional->number == nullptr);
    REQUIRE(present->number == two.get());
    REQUIRE(nodes.dependencies.dependencies(optional.id()).empty());
    REQUIRE(nodes.dependencies.retained(present.id(), two.id()));

    auto snapshot = root.collect<Number>();
    auto four = root.emplace<Number>("four", 4);
    auto list = root.emplace<NumberList>("snapshot", snapshot);
    REQUIRE(list->values.size() == 3);
    REQUIRE(nodes.dependencies.dependencies(list.id()).size() == 3);
    REQUIRE(!nodes.dependencies.retained(list.id(), four.id()));
    auto emptyBranch = root.branch("empty-branch");
    auto empty = root.emplace<NumberList>("empty", emptyBranch.children<Number>());
    REQUIRE(empty->values.empty());
    REQUIRE(nodes.dependencies.dependencies(empty.id()).empty());
}

TEST(Composition_RetainedEdgesSurviveMaintainAndRetireDependents, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto& nodes = fixture.run_ctx.nodes;
    auto provider = root.emplace<Number>("provider", 7);
    auto first = root.emplace<NeedsNumber>("first", provider);
    auto second = root.emplace<MaybeNumber>("second", root.find<Number>("provider"));
    auto list = root.emplace<NumberList>("list", root.children<Number>());
    nodes.ops.maintain();
    REQUIRE(nodes.dependencies.retained(first.id(), provider.id()));
    nodes.ops.retireBranch(provider.id());
    REQUIRE(nodes.registry.require(first.id()).state == NodeState::Retiring);
    REQUIRE(nodes.registry.require(second.id()).state == NodeState::Retiring);
    REQUIRE(nodes.registry.require(list.id()).state == NodeState::Retiring);
    nodes.ops.collectRetired();
    REQUIRE(!nodes.registry.get(first.id()));
    REQUIRE(!nodes.registry.get(second.id()));
    REQUIRE(!nodes.registry.get(list.id()));
}

TEST(Composition_ExportsBelongToTheirObject, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto& nodes = fixture.run_ctx.nodes;
    auto provider = root.emplace<Number>("provider", 7);
    auto branch = root.branch("consumer");
    auto consumer = branch.emplace<ExportedNumber>("object", provider);
    consumer.param("value", &ExportedNumber::value);
    consumer.action("step", &ExportedNumber::step);
    const auto branchId = branch.id();
    REQUIRE(!nodes.dependencies.retained(branchId, consumer.id()));
    nodes.exports.set(nodes.exports.find(consumer.id(), "value"), int64_t(17));
    REQUIRE(consumer->value == 17);
    nodes.ops.retireBranch(provider.id());
    REQUIRE(nodes.exports.find(consumer.id(), "step") == InvalidExportId);
    REQUIRE(nodes.registry.require(branchId).state == NodeState::Active);
    nodes.ops.collectRetired();
    REQUIRE(!nodes.registry.get(consumer.id()));
    REQUIRE(nodes.registry.get(branchId));
}

TEST(Composition_DestructionFollowsEdgesAcrossOwnershipBranches, RuntimeFixture) {
    events.clear();
    auto root = fixture.build(fixture.root);
    auto early = root.branch("early");
    auto storage = root.emplace<Storage>("late-provider");
    early.emplace<Algorithm>("consumer", storage, std::make_unique<int>(42));
    fixture.run_ctx.nodes.ops.destroyBranch(fixture.root);
    REQUIRE((events == std::vector<int>{1, 2, -2, -1}));
}

TEST(Composition_RejectsExpiredForeignAndUnmanagedHandles, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto& nodes = fixture.run_ctx.nodes;
    auto old = root.emplace<Number>("number", 1);
    nodes.ops.destroyBranch(old.id());
    auto replacement = root.emplace<Number>("number", 2);
    REQUIRE(replacement.id() == old.id());
    bool rejected = false;
    try { root.emplace<NeedsNumber>("expired", old); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);
    Number standalone(3);
    rejected = false;
    try { root.emplace<NeedsNumber>("unmanaged", Ref<Number>{&standalone}); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);
    RuntimeFixture foreign;
    auto other = foreign.build(foreign.root).emplace<Number>("foreign", 4);
    rejected = false;
    try { root.emplace<NeedsNumber>("foreign", other); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);
    REQUIRE(nodes.registry.children(fixture.root).size() == 1);
}

TEST(Composition_SharedAmbiguityAndCollectionRollback, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto& nodes = fixture.run_ctx.nodes;
    auto one = root.emplaceShared<Number>("one", 1);
    auto consumerBranch = root.branch("consumer");
    auto mounted = consumerBranch.mount<Number>();
    auto consumer = consumerBranch.emplace<NeedsNumber>("object", mounted.ref());
    REQUIRE(&consumer->number == one.get());
    REQUIRE(nodes.dependencies.retained(consumer.id(), one.id()));
    auto two = root.emplaceShared<Number>("two", 2);
    bool rejected = false;
    try { consumerBranch.mount<Number>(); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);

    auto snapshot = Selection<Number>{std::vector<Ref<Number>>{two, one}};
    nodes.ops.destroyBranch(one.id());
    // A selection containing an expired identity must fail without partial edges.
    rejected = false;
    try { root.emplace<NumberList>("expired-snapshot", snapshot); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);
    const auto numberType = fixture.run_ctx.blueprints.id<NumberList>();
    REQUIRE(nodes.registry.find("expired-snapshot", fixture.root, numberType) == InvalidNodeId);
    REQUIRE(nodes.dependencies.dependents(two.id()).empty());
}

TEST(Composition_ExplicitAPIsAndRequiredAmbiguity, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    root.emplace<FirstNumber, NumberAPI>("same");
    root.emplace<SecondNumber, NumberAPI>("same");
    REQUIRE(root.collect<NumberAPI>().size() == 2);
    bool rejected = false;
    try { root.require<NumberAPI>("same"); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);
    rejected = false;
    try { root.require<NumberAPI>("missing"); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);
    struct APIList {
        explicit APIList(std::vector<NumberAPI*> values) : values(std::move(values)) {}
        std::vector<NumberAPI*> values;
    };
    auto list = root.emplace<APIList>("all", root.collect<NumberAPI>());
    REQUIRE(list->values.size() == 2);
    REQUIRE(list->values[0]->value() + list->values[1]->value() == 3);
}

namespace {
struct ExternalResource {
    static inline int destroyed = 0;
    int stopCalls = 0;
    bool complete = false;
    void requestStop() { ++stopCalls; }
    bool stopped() const { return complete; }
    ~ExternalResource() { ++destroyed; }
};
struct ResourceUser {
    explicit ResourceUser(ExternalResource& resource) : resource(resource) {}
    ExternalResource& resource;
};
struct ExportMethods {
    int value = 0;
    void add(int amount) { value += amount; }
    int read() const { return value; }
};
static_assert(!std::is_copy_constructible_v<NodeBuild>);
static_assert(!std::is_copy_assignable_v<NodeBuild>);
static_assert(std::is_nothrow_move_constructible_v<NodeBuild>);
static_assert(!std::is_copy_constructible_v<MountBuild<ExternalResource>>);
static_assert(std::is_nothrow_move_constructible_v<MountBuild<ExternalResource>>);
static_assert(!std::is_copy_constructible_v<ObjectBuild<ExternalResource>>);
static_assert(std::is_nothrow_move_constructible_v<ObjectBuild<ExternalResource>>);
static_assert(std::is_copy_constructible_v<Ref<ExternalResource>>);
}

TEST(Composition_ExternalLifecycleWaitsForEachInstance, RuntimeFixture) {
    ExternalResource::destroyed = 0;
    auto root = fixture.build(fixture.root);
    auto first = root.emplace<ExternalResource>("first");
    auto second = root.emplace<ExternalResource>("second");
    first.onRetire(&ExternalResource::requestStop);
    first.readyToDestroy(&ExternalResource::stopped);
    second.onRetire([](ExternalResource& resource) { resource.stopCalls += 2; });
    second.readyToDestroy([](const ExternalResource& resource) { return resource.complete; });
    auto user = root.emplace<ResourceUser>("user", first);
    auto& nodes = fixture.run_ctx.nodes;
    nodes.ops.retireBranch(first.id());
    nodes.ops.retireBranch(first.id());
    REQUIRE(first->stopCalls == 1);
    REQUIRE(second->stopCalls == 0);
    REQUIRE(nodes.registry.require(user.id()).state == NodeState::Retiring);
    REQUIRE(nodes.ops.collectRetired() == 0);
    nodes.ops.retireBranch(second.id());
    REQUIRE(second->stopCalls == 2);
    second->complete = true;
    REQUIRE(nodes.ops.collectRetired() == 1);
    REQUIRE(nodes.registry.get(first.id()));
    first->complete = true;
    nodes.ops.collectRetired();
    REQUIRE(ExternalResource::destroyed == 2);
    REQUIRE(!nodes.registry.get(user.id()));
    // Reusing a NodeId must not reuse callbacks from the previous instance.
    auto third = root.emplace<ExternalResource>("third");
    nodes.ops.retireBranch(third.id());
    REQUIRE(third->stopCalls == 0);
    REQUIRE(nodes.ops.collectRetired() == 1);
}

TEST(Composition_RollbackRunsExternalShutdown, RuntimeFixture) {
    ExternalResource::destroyed = 0;
    auto& nodes = fixture.run_ctx.nodes;
    const auto type = fixture.run_ctx.blueprints.addComposition("RollbackLifecycle", +[](NodeBuild root) {
        auto resource = root.emplace<ExternalResource>("resource");
        resource.onRetire([](ExternalResource& resource) {
            resource.requestStop();
            resource.complete = true;
        });
        resource.readyToDestroy(&ExternalResource::stopped);
        throw std::runtime_error("composition failed");
    });
    bool failed = false;
    try { nodes.builder.add(fixture.root, type); }
    catch (const std::runtime_error&) { failed = true; }
    REQUIRE(failed);
    REQUIRE(ExternalResource::destroyed == 1);
    REQUIRE(nodes.registry.children(fixture.root).empty());
}

TEST(Composition_MovedAndExpiredBuildersAreRevoked, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto moved = std::move(root);
    bool rejected = false;
    try { root.branch("invalid"); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);
    auto source = moved.emplaceShared<ExternalResource>("resource");
    auto owner = std::move(source);
    rejected = false;
    try { source.param("stopCalls", &ExternalResource::stopCalls); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);
    auto mount = moved.mount<ExternalResource>();
    auto mounted = std::move(mount);
    rejected = false;
    try { mount.ref(); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);
    REQUIRE(mounted.ref().get() == owner.get());
    auto child = moved.branch("old");
    const auto childId = child.id();
    fixture.run_ctx.nodes.ops.destroyBranch(childId);
    auto replacement = moved.branch("replacement");
    REQUIRE(replacement.id() == childId);
    rejected = false;
    try { child.emplace<Number>("invalid", 1); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE(rejected);
}

TEST(Composition_ObjectExportsSupportArgumentsAndConstMethods, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto object = root.emplace<ExportMethods>("object");
    auto param = object.param("value", &ExportMethods::value);
    auto action = object.action("add", &ExportMethods::add);
    object.action("read", &ExportMethods::read);
    auto& exports = fixture.run_ctx.nodes.exports;
    exports.set(param, int64_t(3));
    auto resolved = exports.resolve(action);
    ActionContext context{object.id()};
    const std::vector<Value> arguments{Value{int64_t(4)}};
    resolved.invoke(resolved.object, context, arguments);
    REQUIRE(object->value == 7);
    REQUIRE(exports.find(root.id(), "value") == InvalidExportId);
    REQUIRE(exports.find(object.id(), "value") == param);
}

namespace {
struct ContextMethods {
    struct Style { std::string prompt = ">"; };
    Style style;
    NodeId selected = InvalidNodeId;
    int calls = 0;
    void select(ActionContext& context, int64_t node) { context.setNode(NodeId(node)); selected = NodeId(node); }
    void show(ActionContext& context) const { context.emit(Value{std::string{"shown"}}); }
    void ping() { ++calls; }
};
}

TEST(Composition_NestsAnotherPackageComposition, RuntimeFixture) {
    auto& blueprints = fixture.run_ctx.blueprints;
    blueprints.addComposition("Inner", +[](NodeBuild root) { root.emplace<Storage>("storage"); });
    blueprints.addComposition("Failing", +[](NodeBuild root) {
        root.emplace<Storage>("storage");
        throw std::runtime_error("inner failed");
    });
    events.clear();
    auto root = fixture.build(fixture.root);
    const NodeId inner = root.compose("Inner", "inner");
    auto& nodes = fixture.run_ctx.nodes;
    REQUIRE(nodes.registry.require(inner).parent == fixture.root);
    REQUIRE(root.collect<Storage>().size() == 1);

    bool unknown = false;
    try { root.compose("Missing"); } catch (const ExceptionBase&) { unknown = true; }
    REQUIRE(unknown);

    // A nested failure rolls back the enclosing package branch as a whole.
    blueprints.addComposition("Outer", +[](NodeBuild root) {
        root.emplace<Storage>("own");
        root.compose("Failing", "failing");
    });
    const auto before = nodes.registry.children(fixture.root).size();
    bool failed = false;
    try { nodes.builder.add(fixture.root, blueprints.resolve("Outer"), "outer"); }
    catch (const std::runtime_error&) { failed = true; }
    REQUIRE(failed);
    REQUIRE(nodes.registry.children(fixture.root).size() == before);
}

TEST(Composition_ObjectActionsReceiveContextAndCanBeGlobal, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto branch = root.branch("branch");
    auto object = branch.emplace<ContextMethods>("object");
    const auto select = object.action("select", &ContextMethods::select);
    object.globalAction("show", &ContextMethods::show);
    object.globalAction("ping", &ContextMethods::ping);
    const auto prompt = object.param("prompt", object->style.prompt);
    auto& nodes = fixture.run_ctx.nodes;

    auto resolved = nodes.exports.resolve(select);
    REQUIRE(resolved.requiredArguments == 1);
    ActionContext context{object.id()};
    const std::vector<Value> arguments{Value{int64_t(branch.id())}};
    resolved.invoke(resolved.object, context, arguments);
    REQUIRE(context.node() == branch.id());
    REQUIRE(object->selected == branch.id());

    // Global actions belong to the object but are visible from the runtime root.
    const ExportsView exports = root.exports();
    auto show = exports.resolve(exports.role("show"), fixture.root);
    REQUIRE(show);
    ActionContext showContext{fixture.root};
    show.invoke(show.object, showContext, {});
    REQUIRE(std::get<Value>(showContext.output().front()).get<std::string>() == "shown");
    REQUIRE(nodes.exports.find(object.id(), "ping") != InvalidExportId);

    nodes.exports.set(prompt, std::string{"$"});
    REQUIRE(object->style.prompt == "$");
    REQUIRE(nodes.exports.find(object.id(), "prompt") == prompt);

    // Composition callbacks receive the object and may receive the context.
    object.action<int64_t>("bump", [](ContextMethods& self, ActionContext& context, int64_t by) {
        self.calls += int(by);
        context.emit(Value{int64_t(self.calls)});
    });
    auto bump = nodes.exports.resolve(nodes.exports.find(object.id(), "bump"));
    ActionContext bumpContext{object.id()};
    const std::vector<Value> by{Value{int64_t(5)}};
    bump.invoke(bump.object, bumpContext, by);
    REQUIRE(object->calls == 5);
    REQUIRE(std::get<Value>(bumpContext.output().front()).get<int64_t>() == 5);

    // Retiring the owner removes its global actions too.
    nodes.ops.retireBranch(object.id());
    REQUIRE(!exports.resolve(exports.role("show"), fixture.root));
}

}

namespace Lattice {
namespace {
void composeRootObject(NodeBuild root) {
    auto storage = root.emplace<Storage>("storage");
    auto algorithm = root.construct<Algorithm>(storage, std::make_unique<int>(42));
    algorithm.param("value", *algorithm->value);
}
void composeRootFailure(NodeBuild root) {
    root.emplace<Storage>("storage");
    root.construct<Throws>(1);
}
}
TEST(Composition_ObjectAtRoot, RuntimeFixture) {
    events.clear();
    auto& nodes = fixture.run_ctx.nodes;
    const auto type = fixture.run_ctx.blueprints.addComposition("RootObject", composeRootObject);
    const auto id = nodes.builder.add(fixture.root, type);
    REQUIRE(nodes.registry.require(id).bp == type);
    const auto object = detail::reference<Algorithm>(nodes, id);
    REQUIRE(object.get() != nullptr);
    REQUIRE(*object->value == 42);
    REQUIRE(nodes.registry.children(id).size() == 1);
    REQUIRE(nodes.dependencies.retained(id, nodes.registry.children(id).front()));
    bool rejected = false;
    try { fixture.build(id).construct<Storage>(); }
    catch (const ExceptionBase&) { rejected = true; }
    REQUIRE(rejected);
    REQUIRE(detail::reference<Algorithm>(nodes, id).get() == object.get());
    nodes.ops.destroyBranch(id);
    REQUIRE((events == std::vector<int>{1, 2, -2, -1}));
}
TEST(Composition_ObjectAtRootRollback, RuntimeFixture) {
    events.clear();
    auto& nodes = fixture.run_ctx.nodes;
    const auto type = fixture.run_ctx.blueprints.addComposition("RootFailure", composeRootFailure);
    bool failed = false;
    try { nodes.builder.add(fixture.root, type); }
    catch (const std::runtime_error&) { failed = true; }
    REQUIRE(failed);
    REQUIRE(nodes.registry.children(fixture.root).empty());
    REQUIRE((events == std::vector<int>{1, -1}));
}
}

namespace Lattice {
TEST(Composition_RootRetiresWithOwnedDependency, RuntimeFixture) {
    events.clear();
    auto& nodes = fixture.run_ctx.nodes;
    const auto type = fixture.run_ctx.blueprints.addComposition("RootRetirement", composeRootObject);
    const auto id = nodes.builder.add(fixture.root, type);
    const auto child = nodes.registry.children(id).front();
    nodes.ops.retireBranch(child);
    nodes.ops.maintain();
    REQUIRE(nodes.registry.get(id) == nullptr);
    REQUIRE((events == std::vector<int>{1, 2, -2, -1}));
}
}

namespace Lattice {
namespace {
struct ViewCamera { int id = 0; };
struct ViewAPI { virtual ~ViewAPI() = default; virtual int camera() const = 0; };
struct View : ViewAPI {
    explicit View(ViewCamera& camera) : camera_(camera) {}
    int camera() const override { return camera_.id; }
    ViewCamera& camera_;
};
struct ViewController {
    explicit ViewController(ViewAPI& view) : view(view) {}
    ViewAPI& view;
};
ObjectBuild<ViewController> composeController(::NodeBuild& slot) {
    return slot.emplace<ViewController>("controller", slot.owner<ViewAPI>());
}
}

TEST(Composition_BranchBecomesObjectNodeAfterChildren, RuntimeFixture,
    "construct на ветке делает её узлом объекта; дети, созданные раньше, остаются его детьми.") {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    auto addView = [&](std::string_view name, int id) {
        auto branch = root.branch(name);
        auto camera = branch.emplace<ViewCamera>("camera");
        camera->id = id;
        auto view = branch.construct<View, ViewAPI>(camera);
        auto controller = branch.addSlot<ViewController>("controls");
        controller.choice("Test::Controller", composeController);
        REQUIRE(&controller->view == static_cast<ViewAPI*>(view.get()));
        return view.id();
    };
    const NodeId first = addView("first", 1);
    const NodeId second = addView("second", 2);

    REQUIRE(nodes.registry.require(first).kind == NodeKind::Object);
    REQUIRE(root.tree().label(first) == "View:first");
    REQUIRE(root.collect<ViewAPI>().size() == 2);
    REQUIRE(root.require<ViewAPI>("second")->camera() == 2);

    nodes.ops.retireBranch(first);
    nodes.ops.maintain();
    REQUIRE(nodes.registry.get(first) == nullptr);
    REQUIRE(root.collect<ViewController>().size() == 1);
    REQUIRE(nodes.registry.get(second) != nullptr);
}

TEST(Composition_OwnerRequiresAnObjectAbove, RuntimeFixture) {
    auto root = fixture.build(fixture.root);
    auto branch = root.branch("plain");
    root.emplace<View, ViewAPI>("sibling", root.emplace<ViewCamera>("camera"));
    bool thrown = false;
    try { branch.owner<ViewAPI>(); }
    catch (const ExceptionBase&) { thrown = true; }
    REQUIRE(thrown);
}
}
