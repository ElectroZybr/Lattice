#include <algorithm>
#include <exception>
#include <ranges>
#include <thread>
#include <chrono>
#include <Lattice/Kernel/NodeOps.hpp>
#include <Lattice/Kernel/NodeRegistry.hpp>
#include <Lattice/Kernel/NodeViews.hpp>
#include <Lattice/Kernel/NodeContext.hpp>
#include <Lattice/Kernel/Blueprints.hpp>
#include <Logger/Logger.hpp>
#include <Lattice/Tools/TreeFormatter.hpp>

namespace Lattice {

void NodeOps::bindService(NodeId id, std::unique_ptr<ServiceRunner> execution) {
    const auto& node = nodeSystem_.registry.require(id);
    if (node.state != NodeState::Active || !node.object.constructed)
        throw Exception<NodeOps>("Service requires an active constructed object");
    if (lifecycle_.size() <= id) lifecycle_.resize(id + 1);
    if (lifecycle_[id].service)
        throw Exception<NodeOps>("Object #{} already declares a service", id);
    lifecycle_[id].service = std::move(execution);
}

ServiceRunner* NodeOps::service(NodeId id) const {
    return id < lifecycle_.size() ? lifecycle_[id].service.get() : nullptr;
}

std::vector<NodeId> NodeOps::services(NodeId root) const {
    auto nodes = collectTree(root);
    nodes.insert(nodes.begin(), root);
    std::erase_if(nodes, [this](NodeId id) { return !service(id); });
    return nodes;
}

void NodeOps::onRetire(NodeId id, std::function<void(void*)> callback) {
    const auto& node = nodeSystem_.registry.require(id);
    if (node.state != NodeState::Active || !node.object.constructed)
        throw Exception<NodeOps>("Lifecycle can only be bound to an active constructed object");
    if (lifecycle_.size() <= id)
        lifecycle_.resize(id + 1);
    lifecycle_[id].retire = std::move(callback);
}

void NodeOps::readyToDestroy(NodeId id, std::function<bool(const void*)> callback) {
    const auto& node = nodeSystem_.registry.require(id);
    if (node.state != NodeState::Active || !node.object.constructed)
        throw Exception<NodeOps>("Lifecycle can only be bound to an active constructed object");
    if (lifecycle_.size() <= id)
        lifecycle_.resize(id + 1);
    lifecycle_[id].ready = std::move(callback);
}

void NodeOps::beginRetire(NodeId id) {
    const auto object = nodeSystem_.registry.require(id).object;
    if (!object.ptr || !object.constructed)
        return;
    if (lifecycle_.size() <= id)
        lifecycle_.resize(id + 1);
    if (lifecycle_[id].retireCalled)
        return;
    lifecycle_[id].retireCalled = true;
    // Copy callbacks before invoking user code, which can grow registry storage.
    const auto callback = lifecycle_[id].retire;
    if (auto* execution = service(id)) execution->retire();
    if (callback)
        callback(object.ptr);
}

bool NodeOps::isReadyToDestroy(NodeId id) const {
    const auto* node = nodeSystem_.registry.get(id);
    if (!node || !node->object.ptr || !node->object.constructed)
        return true;
    if (auto* execution = service(id); execution && !execution->readyToDestroy()) return false;
    const auto callback = id < lifecycle_.size() ? lifecycle_[id].ready : nullptr;
    return !callback || callback(node->object.ptr);
}

NodeId NodeOps::resolvePath(NodeId from, std::string_view path) const {
    NodeId current = from;

    size_t begin = 0;

    while (begin < path.size()) {
        size_t end = path.find('/', begin);
        if (end == std::string_view::npos)
            end = path.size();

        const std::string_view name = path.substr(begin, end - begin);

        if (!name.empty()) {
            current = nodeSystem_.registry.find(name, current);

            if (current == InvalidNodeId)
                return InvalidNodeId;
        }

        begin = end + 1;
    }

    return current;
}

NodeId NodeOps::root(NodeId id) const {
    nodeSystem_.registry.require(id);

    while (nodeSystem_.registry.require(id).parent != InvalidNodeId)
        id = nodeSystem_.registry.require(id).parent;

    return id;
}

bool NodeOps::isUnder(NodeId id, NodeId ancestor) const {
    while (id != InvalidNodeId) {
        if (id == ancestor)
            return true;

        id = nodeSystem_.registry.require(id).parent;
    }

    return false;
}

std::vector<NodeId> NodeOps::collectTree(NodeId id) const {
    std::vector<NodeId> result;

    auto collect = [&](auto&& self, NodeId current) -> void {
        for (NodeId child : nodeSystem_.registry.children(current)) {
            result.push_back(child);
            self(self, child);
        }
    };

    collect(collect, id);
    return result;
}

std::vector<NodeId> NodeOps::collectRemovalClosure(NodeId id) const {
    nodeSystem_.registry.require(id);

    std::vector<NodeId> result;
    std::vector<bool> included(nodeSystem_.registry.size(), false);

    auto addBranch = [&](auto&& self, NodeId root) -> void {
        const auto* node = nodeSystem_.registry.get(root);
        if (!node || included[root])
            return;

        included[root] = true;
        result.push_back(root);

        for (NodeId child : nodeSystem_.registry.children(root))
            self(self, child);
    };

    addBranch(addBranch, id);

    for (size_t index = 0; index < result.size(); ++index) {
        for (NodeId dependent : nodeSystem_.dependencies.dependents(result[index]))
            addBranch(addBranch, dependent);
    }

    return result;
}

void NodeOps::retireBranch(NodeId id) {
    if (!nodeSystem_.registry.get(id))
        return;

    std::vector<NodeId> closure = collectRemovalClosure(id);
    std::vector<NodeId> newlyRetiring;

    for (NodeId nodeId : closure) {
        auto* node = nodeSystem_.registry.get(nodeId);
        if (!node || node->state == NodeState::Retiring)
            continue;

        node->state = NodeState::Retiring;
        nodeSystem_.context.removeTarget(nodeId);
        nodeSystem_.exports.remove(nodeId);
        newlyRetiring.push_back(nodeId);
    }

    size_t destination = retiring_.size();
    for (size_t index = 0; index < retiring_.size();) {
        const bool overlaps = std::ranges::any_of(retiring_[index], [&closure](NodeId queued) {
            return std::ranges::find(closure, queued) != closure.end();
        });

        if (!overlaps) {
            ++index;
            continue;
        }

        if (destination == retiring_.size()) {
            destination = index;
            ++index;
            continue;
        }

        auto& target = retiring_[destination];
        for (NodeId queued : retiring_[index])
            if (std::ranges::find(target, queued) == target.end())
                target.push_back(queued);
        retiring_.erase(retiring_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    if (destination == retiring_.size()) {
        retiring_.push_back(std::move(closure));
    } else {
        auto& target = retiring_[destination];
        for (NodeId nodeId : closure)
            if (std::ranges::find(target, nodeId) == target.end())
                target.push_back(nodeId);
    }

    std::exception_ptr failure;
    for (NodeId nodeId : newlyRetiring | std::views::reverse) {
        if (!nodeSystem_.registry.get(nodeId))
            continue;
        try {
            beginRetire(nodeId);
        } catch (...) {
            if (!failure)
                failure = std::current_exception();
        }
    }

    if (failure)
        std::rethrow_exception(failure);
}

size_t NodeOps::collectRetired() {
    size_t collected = 0;

    for (size_t index = 0; index < retiring_.size();) {
        const auto ready = std::ranges::all_of(retiring_[index], [this](NodeId nodeId) {
            return isReadyToDestroy(nodeId);
        });

        if (!ready) {
            ++index;
            continue;
        }

        auto nodes = std::move(retiring_[index]);
        retiring_.erase(retiring_.begin() + static_cast<std::ptrdiff_t>(index));
        ++collected;

        // Let owners stop and destroy their objects while owned dependencies live.
        for (NodeId nodeId : nodes) {
            if (!nodeSystem_.registry.get(nodeId))
                continue;
            const auto parent = nodeSystem_.registry.require(nodeId).parent;
            if (std::ranges::find(nodes, parent) != nodes.end())
                continue;
            destroyBranch(nodeId);
        }
    }

    return collected;
}

void NodeOps::maintain() {
    collectRetired();
}

void NodeOps::destroyBranch(NodeId id) {
    if (!nodeSystem_.registry.get(id) ||
        std::ranges::find(destroying_, id) != destroying_.end())
        return;
    destroying_.push_back(id);
    struct DestroyGuard {
        std::vector<NodeId>& active;
        NodeId id;
        ~DestroyGuard() { std::erase(active, id); }
    } guard{destroying_, id};

    // Retained C++ references must remain valid through consumer destruction,
    // even when ownership places consumers in an earlier sibling branch.
    const auto dependentSpan = nodeSystem_.dependencies.dependents(id);
    const std::vector<NodeId> dependents(dependentSpan.begin(), dependentSpan.end());
    for (NodeId dependent : dependents)
        if (nodeSystem_.dependencies.retained(dependent, id) &&
            nodeSystem_.registry.get(dependent) && !isUnder(id, dependent))
            destroyBranch(dependent);

    if (!nodeSystem_.registry.get(id))
        return;

    clearContents(id);

    nodeSystem_.context.removeTarget(id);
    nodeSystem_.exports.remove(id);

    if (const ContextScopeId scope = nodeSystem_.context.findScope(id); scope != InvalidContextScopeId)
        nodeSystem_.context.destroyScope(scope);

    nodeSystem_.dependencies.removeNode(id);
    nodeSystem_.registry.destroy(id);
}

void NodeOps::clearContents(NodeId id) {
    beginRetire(id);
    if (service(id) || (id < lifecycle_.size() && lifecycle_[id].ready))
        while (!isReadyToDestroy(id))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

    if (id < lifecycle_.size()) {
        auto execution = std::move(lifecycle_[id].service);
        execution.reset(); // Join while the object and its dependencies still exist.
    }

    const auto children = std::vector<NodeId>(nodeSystem_.registry.children(id).begin(), nodeSystem_.registry.children(id).end());

    const auto object = nodeSystem_.registry.require(id).object;

    if (object.ptr) {
        const auto ops = nodeSystem_.blueprints.require(object.bp).meta.ops;

        if (ops) {
            if (object.constructed && ops->destroy)
                ops->destroy(object.ptr);
            else if (!object.constructed && ops->deallocate)
                ops->deallocate(object.ptr);
        }

        nodeSystem_.registry.require(id).object = {};
    }
    nodeSystem_.registry.require(id).object = {};
    for (NodeId child : children | std::views::reverse)
        if (nodeSystem_.registry.get(child))
            destroyBranch(child);
    if (nodeSystem_.registry.require(id).kind == NodeKind::Slot)
        nodeSystem_.registry.require(id).relation = InvalidNodeId;
    if (id < lifecycle_.size())
        lifecycle_[id] = {};
}

std::string NodeOps::stringPath(NodeId id) const {
    std::vector<NodeId> path;

    for (NodeId current = id; current != InvalidNodeId; current = nodeSystem_.registry.require(current).parent)
        path.push_back(current);

    std::ranges::reverse(path);

    std::string result;

    for (NodeId current : path) {
        const auto& node = nodeSystem_.registry.require(current);

        if (!result.empty())
            result += '/';

        if (node.type() == InvalidBlueprintId) {
            result += node.name;
            continue;
        }

        const auto name = nodeSystem_.blueprints.require(node.type()).shortName();

        if (node.name.empty())
            result += name;
        else
            result += std::format("{}:{}", name, node.name);
    }

    return result;
}

void NodeOps::dumpTree(NodeId id, NodeId highlighted) const {
    const auto& root = nodeSystem_.registry.require(id);
    Lattice::TreeFormatter tree(root.name.empty() ? "Root" : root.name);

    auto append = [&](auto&& self, NodeId current, size_t depth) -> void {
        for (NodeId childId : nodeSystem_.registry.children(current)) {
            const auto& child = nodeSystem_.registry.require(childId);
            std::string line;

            switch (child.kind) {
                case NodeKind::Folder:
                    line = std::format("{} <a>F</>", child.name);
                    break;

                case NodeKind::Object: {
                    const auto type = nodeSystem_.blueprints.require(child.type()).shortName();
                    line = child.name.empty()
                        ? std::format("{} <ok>C</>", type)
                        : std::format("{}<mut2>::{}</> <ok>C</>", type, child.name);
                    break;
                }

                case NodeKind::Slot: {
                    const auto type = nodeSystem_.blueprints.require(child.bp).shortName();
                    line = child.name.empty()
                        ? std::string(type)
                        : std::format("{}<mut2>::{}</>", type, child.name);

                    line += child.object.ptr
                        ? std::format("<mut2>::</><h>{}</> <h>S</>", nodeSystem_.blueprints.require(child.object.bp).shortName())
                        : "<mut2>::</><h>empty</> <h>S</>";
                    break;
                }

                case NodeKind::Binding:
                    line = std::format("{} <wrn>λ</>", child.name);
                    break;

            }

            if (service(childId)) line += " <a2>[service]</>";
            if (childId == highlighted)
                line = std::format("<err>{} 🡸</>", line);

            line += std::format(" <mut2>#{}</>", childId);

            tree.node(line, depth);
            self(self, childId, depth + 1);
        }
    };

    append(append, id, 0);
    Logger::message(tree.format());
    Logger::blank();
}

void NodeOps::dumpContext() const {
    Lattice::TreeFormatter tree("Context");

    const auto label = [this](NodeId id) -> std::string {
        if (id == InvalidNodeId) return "Empty";
        return nodeSystem_.registry.get(id) ? stringPath(id) : std::format("missing #{}", id);
    };

    tree.node("<h>Scopes</>", 0);

    for (ContextScopeId id = 0; id < nodeSystem_.context.scopeCount(); ++id) {
        const auto* scope = nodeSystem_.context.scope(id);
        if (!scope) continue;

        std::string state;
        if (id == nodeSystem_.context.root()) state += " <a>[root]</>";
        if (nodeSystem_.context.isActive(id)) state += " <a>[active]</>";

        const auto type = scope->type == InvalidBlueprintId ? std::string_view{"untyped"} : nodeSystem_.blueprints.require(scope->type).name;

        tree.node(std::format("{}{} <mut2>#{} [{}]</>", label(scope->owner), state, id, type), 1);

        for (const auto& entry : scope->roles)
            tree.node(std::format("{} ➜ <mut2>{}</>", nodeSystem_.context.roleName(entry.role), label(entry.target)), 2);
    }

    tree.node("<h>Resolved roles</>", 0);

    for (RoleId role = 0; role < nodeSystem_.context.roleCount(); ++role) {
        if (!nodeSystem_.context.hasRole(role)) continue;
        tree.node(std::format("{} ➜ <mut2>{}</>", nodeSystem_.context.roleName(role), label(nodeSystem_.context.resolve(role))), 1);
    }

    Logger::message(tree.format());
    Logger::blank();
}

}
