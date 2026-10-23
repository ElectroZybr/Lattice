#pragma once

#include <span>
#include <string_view>
#include <vector>

#include <Lattice/Kernel/Node.hpp>
#include <Lattice/Tools/ObjectRegistry.hpp>

namespace Lattice {

class NodeRegistry : public ObjectRegistry<Node, NodeId, NodeKey, NodeKeyHash> {
    using Base = ObjectRegistry<Node, NodeId, NodeKey, NodeKeyHash>;
    std::vector<std::vector<NodeId>> children_;
    std::vector<uint64_t> generations_;
    uint64_t nextGeneration_ = 1;
    
public:
    void clear() {
        children_.clear();
        generations_.clear();
        Base::clear();
    }
    
    NodeId create(Node node, uint64_t discriminator = std::numeric_limits<uint64_t>::max()) {
        if (node.parent != InvalidNodeId)
            require(node.parent);

        NodeKey key{node.name, node.parent, node.bp, discriminator};
        const NodeId id = Base::create(std::move(node), std::move(key));

        if (id >= children_.size()) {
            children_.resize(id + 1);
            generations_.resize(id + 1);
        }
        generations_[id] = nextGeneration_++;

        const NodeId parent = require(id).parent;
        if (parent != InvalidNodeId)
            children_[parent].push_back(id);

        children_[id].clear();

        return id;
    }

    uint64_t generation(NodeId id) const {
        require(id);
        return generations_[id];
    }

    NodeId find(
        std::string_view name,
        NodeId parent = InvalidNodeId,
        BlueprintId type = InvalidBlueprintId,
        uint64_t discriminator = std::numeric_limits<uint64_t>::max()
    ) const {
        return Base::find(NodeKey{std::string(name), parent, type, discriminator});
    }

    std::span<const NodeId> children(NodeId id) const {
        require(id);
        return children_[id];
    }

    void destroy(NodeId id) {
        const NodeId parent = require(id).parent;

        const auto childIds = std::vector<NodeId>(children(id).begin(), children(id).end());
        for (NodeId child : childIds)
            destroy(child);

        children_[id].clear();

        if (parent != InvalidNodeId) {
            auto& siblings = children_[parent];
            std::erase(siblings, id);
        }

        Base::destroy(id);
    }
};

}
