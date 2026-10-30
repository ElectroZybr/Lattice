#pragma once

#include <string>

#include <Lattice/kernel/Blueprints.hpp>
#include <Lattice/kernel/Consts.hpp>

namespace Lattice {

enum class NodeState : uint8_t {
    Active,
    Retiring
};

struct RuntimeObject {
    void* ptr = nullptr;
    BlueprintId bp = InvalidBlueprintId;
    bool building = false;
    bool constructed = false;
};

struct Node {
    std::string name;
    NodeId parent = InvalidNodeId;
    BlueprintId bp = InvalidBlueprintId;
    /// Slot, or a child built from an implementation: the implementation (composition),
    /// independent of the node's type.
    BlueprintId choice = InvalidBlueprintId;
    NodeKind kind = NodeKind::Folder;
    NodeState state = NodeState::Active;
    RuntimeObject object;

    /// Declared type, or the object's type for a branch that became an object node.
    BlueprintId type() const noexcept { return bp != InvalidBlueprintId ? bp : object.bp; }
    bool chosen() const noexcept { return kind == NodeKind::Slot || choice != InvalidBlueprintId; }
    /// What composes the node: its implementation, else its own blueprint.
    BlueprintId composition() const noexcept { return chosen() ? choice : bp; }
    /// What it was built from: its implementation, else its object's type.
    BlueprintId implementation() const noexcept { return chosen() ? choice : object.bp; }
};

struct NodeKey {
    std::string name;
    NodeId parent = InvalidNodeId;
    BlueprintId type = InvalidBlueprintId;
    uint64_t discriminator = std::numeric_limits<uint64_t>::max();

    bool operator==(const NodeKey&) const = default;
};

struct NodeKeyHash {
    size_t operator()(const NodeKey& key) const noexcept {
        size_t h = std::hash<NodeId>{}(key.parent);
        h ^= std::hash<std::string>{}(key.name) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<BlueprintId>{}(key.type) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(key.discriminator) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

}
