#include <Lattice/Kernel/NodeQuery.hpp>

#include <Lattice/Kernel/Blueprints.hpp>
#include <Lattice/Tools/Exception.hpp>
#include <Lattice/Kernel/NodeRegistry.hpp>
#include <Lattice/Kernel/NodeContext.hpp>

namespace Lattice {

bool NodeQuery::provides(NodeId id, BlueprintId api) const {
    const auto& node = registry_.require(id);

    if (node.state == NodeState::Retiring)
        return false;

    return blueprints_.isA(node.bp, api) ||
           blueprints_.isA(node.object.bp, api);
}

NodeId NodeQuery::find(NodeId from, BlueprintId api, std::string_view instance) const {
    blueprints_.require(api);
    if (registry_.require(from).state == NodeState::Retiring)
        return InvalidNodeId;

    for (NodeId current = from; current != InvalidNodeId; current = registry_.require(current).parent) {
        NodeId result = InvalidNodeId;
        for (NodeId child : registry_.children(current)) {
            const auto& node = registry_.require(child);

            if (node.name != instance)
                continue;

            if (provides(child, api)) {
                if (result != InvalidNodeId)
                    throw Exception<NodeQuery>("Ambiguous '{}' with instance '{}' under #{}",
                                    blueprints_.require(api).name, instance, current);
                result = child;
            }
        }
        if (result != InvalidNodeId) return result;
    }

    return InvalidNodeId;
}

NodeId NodeQuery::require(NodeId from, BlueprintId api, std::string_view instance) const {
    const NodeId id = find(from, api, instance);

    if (id == InvalidNodeId)
        throw Exception<NodeQuery>("Object '{}' with instance '{}' not found",
                        blueprints_.require(api).name, instance);

    if (!resolve(id, api))
        throw Exception<NodeQuery>("Slot '{}' with instance '{}' is empty",
                        blueprints_.require(api).name, instance);

    return id;
}

NodeId NodeQuery::find(NodeId from, std::string_view api, std::string_view instance) const {
    const BlueprintId id = blueprints_.resolve(api);
    return id == InvalidBlueprintId ? InvalidNodeId : find(from, id, instance);
}

NodeId NodeQuery::require(NodeId from, std::string_view api, std::string_view instance) const {
    const BlueprintId id = blueprints_.resolve(api);

    if (id == InvalidBlueprintId)
        throw Exception<NodeQuery>("Unknown blueprint '{}'", api);

    return require(from, id, instance);
}

std::vector<NodeId> NodeQuery::collect(NodeId from, BlueprintId api) const {
    registry_.require(from);
    blueprints_.require(api);

    std::vector<NodeId> result;
    std::vector<NodeId> slotObjects;

    auto walk = [&](auto&& self, NodeId current) -> void {
        if (registry_.require(current).state == NodeState::Retiring)
            return;

        if (provides(current, api) && std::ranges::find(slotObjects, current) == slotObjects.end()) {
            result.push_back(current);
            const auto& node = registry_.require(current);
            if (node.kind == NodeKind::Slot && node.relation != InvalidNodeId)
                slotObjects.push_back(objectNode(current));
        }

        for (NodeId child : registry_.children(current))
            self(self, child);
    };

    walk(walk, from);

    return result;
}

std::vector<NodeId> NodeQuery::collect(NodeId from, std::string_view api) const {
    const BlueprintId id = blueprints_.resolve(api);

    if (id == Blueprints::InvalidId)
        return {};

    return collect(from, id);
}

NodeId NodeQuery::objectNode(NodeId id) const {
    const auto* node = registry_.get(id);
    if (!node || node->state == NodeState::Retiring || node->object.building)
        return InvalidNodeId;
    if (node->kind == NodeKind::Slot && node->relation != InvalidNodeId)
        return objectNode(node->relation);
    return node->object.ptr ? id : InvalidNodeId;
}

void* NodeQuery::resolve(NodeId id, BlueprintId api) const {
    const auto object = objectNode(id);
    if (object == InvalidNodeId)
        return nullptr;
    const auto& node = registry_.require(object);
    return blueprints_.cast(node.object.bp, api, node.object.ptr);
}

void* NodeQuery::resolve(NodeId id, std::string_view api) const {
    const BlueprintId bp = blueprints_.resolve(api);

    if (bp == Blueprints::InvalidId)
        return nullptr;

    return resolve(id, bp);
}

NodeId NodeQuery::shared(NodeId from, BlueprintId api) const {
    const RoleId role = context_.findRole(blueprints_.require(api).name);

    if (role == InvalidRoleId)
        throw Exception<NodeQuery>("Shared '{}' not found", blueprints_.require(api).name);

    const auto scope = context_.nearestScope(from);
    const auto result = context_.resolveInfo(scope, role);

    if (result.state == ContextResolutionState::Missing)
        throw Exception<NodeQuery>("Shared '{}' not found", blueprints_.require(api).name);

    if (result.state == ContextResolutionState::Ambiguous)
        throw Exception<NodeQuery>("Shared '{}' is ambiguous", blueprints_.require(api).name);

    return result.target;
}

}
