#pragma once

#include <span>
#include <vector>

#include <Lattice/kernel/Consts.hpp>
#include <Lattice/kernel/Visibility.hpp>

namespace Lattice {

class LT_API DependencyGraph {
public:
    void add(NodeId dependent, NodeId dependency);
    void retain(NodeId dependent, NodeId dependency);
    bool retained(NodeId dependent, NodeId dependency) const;
    void clearResolvedDependencies(NodeId node);
    void clearDependencies(NodeId node);
    void removeNode(NodeId node);
    void clear();

    std::span<const NodeId> dependencies(NodeId node) const;
    std::span<const NodeId> dependents(NodeId node) const;

private:
    void ensure(NodeId node);

    std::vector<std::vector<NodeId>> retained_;
    std::vector<std::vector<NodeId>> dependencies_;
    std::vector<std::vector<NodeId>> dependents_;
};

}
