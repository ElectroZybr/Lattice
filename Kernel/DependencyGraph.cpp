#include <algorithm>

#include <Lattice/Kernel/DependencyGraph.hpp>
#include <Lattice/Tools/Exception.hpp>

namespace Lattice {

void DependencyGraph::ensure(NodeId node) {
    if (node == InvalidNodeId)
        throw Exception<DependencyGraph>("Invalid node id");

    const size_t size = static_cast<size_t>(node) + 1;
    if (dependencies_.size() < size) {
        retained_.resize(size);
        dependencies_.resize(size);
        dependents_.resize(size);
    }
}

void DependencyGraph::add(NodeId dependent, NodeId dependency) {
    ensure(dependent);
    ensure(dependency);

    if (dependent == dependency)
        return;

    auto& dependencies = dependencies_[dependent];
    if (std::ranges::find(dependencies, dependency) != dependencies.end())
        return;

    dependencies.push_back(dependency);
    dependents_[dependency].push_back(dependent);
}

void DependencyGraph::retain(NodeId dependent, NodeId dependency) {
    add(dependent, dependency);
    if (dependent != dependency && !retained(dependent, dependency))
        retained_[dependent].push_back(dependency);
}

bool DependencyGraph::retained(NodeId dependent, NodeId dependency) const {
    return dependent < retained_.size() &&
        std::ranges::find(retained_[dependent], dependency) != retained_[dependent].end();
}

void DependencyGraph::clearResolvedDependencies(NodeId node) {
    if (node >= dependencies_.size())
        return;
    std::erase_if(dependencies_[node], [&](NodeId dependency) {
        if (retained(node, dependency))
            return false;
        std::erase(dependents_[dependency], node);
        return true;
    });
}

void DependencyGraph::clearDependencies(NodeId node) {
    if (node >= dependencies_.size())
        return;

    for (NodeId dependency : dependencies_[node])
        std::erase(dependents_[dependency], node);

    dependencies_[node].clear();
    retained_[node].clear();
}

void DependencyGraph::removeNode(NodeId node) {
    if (node >= dependencies_.size())
        return;

    clearDependencies(node);

    for (NodeId dependent : dependents_[node]) {
        std::erase(dependencies_[dependent], node);
        std::erase(retained_[dependent], node);
    }

    dependents_[node].clear();
}

void DependencyGraph::clear() {
    retained_.clear();
    dependencies_.clear();
    dependents_.clear();
}

std::span<const NodeId> DependencyGraph::dependencies(NodeId node) const {
    return node < dependencies_.size()
        ? std::span<const NodeId>{dependencies_[node]}
        : std::span<const NodeId>{};
}

std::span<const NodeId> DependencyGraph::dependents(NodeId node) const {
    return node < dependents_.size()
        ? std::span<const NodeId>{dependents_[node]}
        : std::span<const NodeId>{};
}

}
