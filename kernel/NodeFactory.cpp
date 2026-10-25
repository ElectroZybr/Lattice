#include <format>
#include <string>
#include <Lattice/kernel/NodeFactory.hpp>
#include <Lattice/kernel/NodeViews.hpp>
#include <Lattice/kernel/NodeRegistry.hpp>
#include <Lattice/kernel/NodeContext.hpp>
#include <Lattice/kernel/Node.hpp>
#include <Lattice/kernel/NodeOps.hpp>
#include "Lattice/kernel/Blueprints.hpp"
#include "Lattice/kernel/Consts.hpp"
#include "lattice-logger/Logger.hpp"

namespace Lattice {

namespace {
constexpr uint64_t NormalIdentity = std::numeric_limits<uint64_t>::max();
}

NodeId NodeFactory::createNode(
    NodeId parent,
    std::string_view name,
    BlueprintId bp,
    NodeKind kind,
    uint64_t discriminator
) {
    NodeId previous = InvalidNodeId;

    if (parent != InvalidNodeId &&
        nodeSystem_.registry.require(parent).state == NodeState::Retiring)
        throw Exception<NodeFactory>("Cannot create a node under retiring parent #{}", parent);

    if (kind == NodeKind::Object && discriminator == NormalIdentity &&
        parent != InvalidNodeId && bp != InvalidBlueprintId) {
        for (NodeId child : nodeSystem_.registry.children(parent)) {
            if (nodeSystem_.registry.require(child).bp == bp) {
                previous = child;
                break;
            }
        }
    }

    Node node{
        .name = std::string(name),
        .parent = parent,
        .bp = bp,
        .kind = kind
    };

    const NodeId id = nodeSystem_.registry.create(std::move(node), discriminator);

    if (previous != InvalidNodeId) {
        const RoleId role = nodeSystem_.context.role(nodeSystem_.blueprints.require(bp).name);
        const ContextScopeId scope = nodeSystem_.context.createScope(parent);

        nodeSystem_.context.addCandidate(scope, role, previous);
        nodeSystem_.context.addCandidate(scope, role, id);
    }

    std::string label;
    
    if (bp != InvalidBlueprintId) 
        label += nodeSystem_.blueprints.require(bp).name;
    if (!name.empty())
        label += std::format(":{}", name);

    Logger::info("NodeFactory", "added node '{}'", label);

    if (parent != InvalidNodeId)
        nodeSystem_.ops.invalidate(parent);
    return id;
}

NodeId NodeFactory::emplace(
    NodeId parent, BlueprintId type, std::string_view instance,
    void (*construct)(void*, NodeId, void*), void* arguments
) {
    const auto meta = nodeSystem_.blueprints.require(type).meta;
    if (!meta.allocate || !meta.ops || !construct)
        throw Exception<NodeFactory>("Blueprint has no object storage operations");
    if (nodeSystem_.registry.find(instance, parent, type) != InvalidNodeId)
        throw Exception<NodeFactory>("Object '{}' already exists under node #{}", instance, parent);

    const NodeId id = createNode(parent, instance, type, NodeKind::Object);
    this->construct(id, type, construct, arguments);
    return id;
}

void NodeFactory::construct(NodeId id, BlueprintId type,
    void (*constructor)(void*, NodeId, void*), void* arguments) {
    const auto meta = nodeSystem_.blueprints.require(type).meta;
    if (!meta.allocate || !meta.ops || !constructor)
        throw Exception<NodeFactory>("Blueprint has no object storage operations");
    {
        auto& node = nodeSystem_.registry.require(id);
        // A slot being composed holds its implementation object itself.
        const bool slot = node.kind == NodeKind::Slot && node.object.building && !node.object.ptr;
        const bool holder = node.kind == NodeKind::Object || node.kind == NodeKind::Folder;
        if (node.state != NodeState::Active || node.object.ptr || (!slot && (!holder || node.object.building)))
            throw Exception<NodeFactory>("Node #{} cannot hold another object", id);
        // A structural branch becomes the node of its object; its children stay owned by it.
        if (!slot)
            node.kind = NodeKind::Object;
    }
    try {
        void* storage = meta.allocate();
        nodeSystem_.registry.require(id).object = {
            .ptr = storage, .bp = type, .building = true, .constructed = false
        };
        constructor(storage, id, arguments);
        auto& object = nodeSystem_.registry.require(id).object;
        object.constructed = true;
        object.building = false;
        nodeSystem_.ops.invalidate(id);
    } catch (...) {
        nodeSystem_.ops.destroyBranch(id);
        throw;
    }
}

NodeId NodeFactory::folder(NodeId parent, std::string_view name) {
    return createNode(parent, name, InvalidBlueprintId, NodeKind::Folder);
}

void NodeFactory::constructObject(NodeId id, BlueprintId implementation) {
    const auto blueprint = nodeSystem_.blueprints.require(implementation);

    // configure runs once the composition is complete, then again on tree changes.
    struct Composing {
        NodeOps& ops;
        NodeId id;
        bool done = false;
        ~Composing() { if (!done) ops.abortCompose(id); }
    };

    if (blueprint.meta.composeSlot) {
        auto build = nodeSystem_.builder.node(id);
        nodeSystem_.registry.require(id).choice = implementation;
        nodeSystem_.registry.require(id).object.building = true;
        nodeSystem_.ops.beginCompose(id);
        Composing composing{nodeSystem_.ops, id};
        const auto target = blueprint.meta.composeSlot(build);
        // The implementation object lives on the slot itself (slot.construct<Impl, API>).
        auto& node = nodeSystem_.registry.require(id);
        node.object.building = false;
        if (target != id || !node.object.ptr || !nodeSystem_.query.resolve(id, node.bp))
            throw Exception<NodeFactory>("Slot composition must construct its API object on the slot");
        composing.done = true;
        nodeSystem_.ops.endCompose(id);
        return;
    }

    if (blueprint.meta.compose) {
        nodeSystem_.registry.require(id).object.bp = implementation;
        nodeSystem_.ops.beginCompose(id);
        Composing composing{nodeSystem_.ops, id};
        blueprint.meta.compose(nodeSystem_.builder.node(id));
        composing.done = true;
        nodeSystem_.ops.endCompose(id);
        return;
    }

    throw Exception<NodeFactory>("Blueprint '{}' is not composable", blueprint.name);
}

void* NodeFactory::resolve(NodeId id, BlueprintId api) const {
    return nodeSystem_.query.resolve(id, api);
}

NodeId NodeFactory::component(NodeId parent, BlueprintId api, std::string_view instance) {
    nodeSystem_.blueprints.require(api);

    const BlueprintId impl = nodeSystem_.blueprints.resolveImplementation(api);

    if (const NodeId existing = nodeSystem_.registry.find(instance, parent, impl); existing != InvalidNodeId) {
        const auto& node = nodeSystem_.registry.require(existing);

        if (node.state == NodeState::Retiring)
            throw Exception<NodeFactory>("Node #{} is retiring", existing);

        if (node.kind == NodeKind::Object && node.bp == impl)
            return existing;

        throw Exception<NodeFactory>("Node '{}' already exists under parent #{}", instance, parent);
    }

    const NodeId id = createNode(parent, instance, impl, NodeKind::Object);
    try {
        constructObject(id, impl);
    } catch (...) {
        nodeSystem_.ops.destroyBranch(id);

        throw;
    }

    return id;
}

NodeId NodeFactory::component(NodeId parent, std::string_view blueprint, std::string_view instance) {
    const BlueprintId id = nodeSystem_.blueprints.resolve(blueprint);
    if (id == InvalidBlueprintId)
        throw Exception<NodeFactory>("Unknown blueprint '{}'", blueprint);
    return component(parent, id, instance);
}

NodeId NodeFactory::slot(NodeId parent, std::string_view api, std::string_view instance) {
    const BlueprintId id = nodeSystem_.blueprints.resolve(api);
    if (id == InvalidBlueprintId)
        throw Exception<NodeFactory>("Unknown API '{}'", api);
    return slot(parent, id, instance);
}

NodeId NodeFactory::slot(NodeId parent, BlueprintId api, std::string_view instance) {
    nodeSystem_.blueprints.require(api);

    if (const NodeId existing = nodeSystem_.registry.find(instance, parent, api); existing != InvalidNodeId) {
        const auto& node = nodeSystem_.registry.require(existing);
        if (node.state == NodeState::Retiring)
            throw Exception<NodeFactory>("Node #{} is retiring", existing);
        if (node.kind == NodeKind::Slot && node.bp == api)
            return existing;
    }

    const NodeId id = createNode(parent, instance, api, NodeKind::Slot);

    const RoleId role = nodeSystem_.context.role(nodeSystem_.blueprints.require(api).name);
    const ContextScopeId scope = nodeSystem_.context.createScope(parent);

    nodeSystem_.context.addCandidate(scope, role, id);

    return id;
}

bool NodeFactory::choice(NodeId id, BlueprintId impl) {
    const BlueprintId api = nodeSystem_.registry.require(id).bp;
    const BlueprintId current = nodeSystem_.registry.require(id).choice;

    if (nodeSystem_.registry.require(id).state == NodeState::Retiring)
        throw Exception<NodeFactory>("Node #{} is retiring", id);

    if (nodeSystem_.registry.require(id).kind != NodeKind::Slot)
        throw Exception<NodeFactory>("Node #{} is not a slot", id);

    if (!nodeSystem_.blueprints.isA(impl, api))
        throw Exception<NodeFactory>("'{}' does not implement '{}'",
            nodeSystem_.blueprints.require(impl).name,
            nodeSystem_.blueprints.require(api).name);

    if (current == impl && !nodeSystem_.registry.require(id).object.building)
        return true;

    const auto blueprint = nodeSystem_.blueprints.require(impl);
    if (!blueprint.meta.compose && !blueprint.meta.composeSlot)
        throw Exception<NodeFactory>("Blueprint '{}' is not composable", nodeSystem_.blueprints.require(impl).name);

    // Composed slots have no object at the slot node: retire their owned subtree
    // through the normal lifecycle, keeping the slot identity/context candidate alive.
    const auto currentBlueprint = nodeSystem_.blueprints.get(current);
    if (currentBlueprint && currentBlueprint->meta.composeSlot) {
        // The implementation object is on the slot and depends on the slot's children:
        // they go together in clearContents. External consumers retire first.
        const auto dependents = std::vector<NodeId>(nodeSystem_.dependencies.dependents(id).begin(),
                                                    nodeSystem_.dependencies.dependents(id).end());
        // An ancestor consumer would retire the slot itself; require an external
        // branch rebuild instead of silently removing the handle being replaced.
        for (auto dependent : dependents)
            if (nodeSystem_.ops.isUnder(id, dependent))
                throw Exception<NodeFactory>("Slot replacement would retire its owner; rebuild the owning branch");
        nodeSystem_.registry.require(id).object.building = true;
        nodeSystem_.exports.remove(id);
        if (const auto scope = nodeSystem_.context.findScope(id); scope != InvalidContextScopeId)
            nodeSystem_.context.destroyScope(scope);
        for (auto dependent : dependents)
            nodeSystem_.ops.retireBranch(dependent);
        const bool ready = nodeSystem_.ops.retireObject(id);
        if (!dependents.empty() || !ready)
            return false;
    }

    for (NodeId dependent : nodeSystem_.dependencies.dependents(id))
        if (nodeSystem_.dependencies.retained(dependent, id))
            throw Exception<NodeFactory>("Cannot replace slot #{} while constructor/export dependencies exist", id);

    nodeSystem_.ops.clearContents(id);
    nodeSystem_.dependencies.clearDependencies(id);
    nodeSystem_.exports.remove(id);
    if (const auto scope = nodeSystem_.context.findScope(id); scope != InvalidContextScopeId)
        nodeSystem_.context.destroyScope(scope);
    try {
        constructObject(id, impl);
    } catch (...) {
        nodeSystem_.ops.clearContents(id);
        nodeSystem_.dependencies.clearDependencies(id);
        nodeSystem_.exports.remove(id);
        if (const auto scope = nodeSystem_.context.findScope(id); scope != InvalidContextScopeId)
            nodeSystem_.context.destroyScope(scope);
        throw;
    }
    return true;
}

void NodeFactory::share(NodeId id, BlueprintId api) {
    const auto& node = nodeSystem_.registry.require(id);
    nodeSystem_.blueprints.require(api);

    if (node.state == NodeState::Retiring)
        throw Exception<NodeFactory>("Node #{} is retiring", id);

    if (!nodeSystem_.blueprints.isA(node.bp, api) && !nodeSystem_.blueprints.isA(node.object.bp, api))
        throw Exception<NodeFactory>("'{}' does not implement '{}'",
            nodeSystem_.blueprints.require(node.type()).name,
            nodeSystem_.blueprints.require(api).name);

    NodeId root = id;
    while (nodeSystem_.registry.require(root).parent != InvalidNodeId)
        root = nodeSystem_.registry.require(root).parent;

    const ContextScopeId scope = nodeSystem_.context.createScope(root);
    const RoleId role = nodeSystem_.context.role(nodeSystem_.blueprints.require(api).name);

    nodeSystem_.context.addCandidate(scope, role, id);
}

NodeId NodeFactory::binding(NodeId parent, std::string_view name) {
    if (const NodeId existing = nodeSystem_.registry.find(name, parent); existing != InvalidNodeId) {
        const auto& node = nodeSystem_.registry.require(existing);

        if (node.state == NodeState::Retiring)
            throw Exception<NodeFactory>("Node #{} is retiring", existing);

        if (node.kind == NodeKind::Binding)
            return existing;

        throw Exception<NodeFactory>("Node '{}' already exists under parent #{}", name, parent);
    }

    return createNode(parent, name, InvalidBlueprintId, NodeKind::Binding);
}

}
