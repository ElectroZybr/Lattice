#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Lattice/Kernel/Consts.hpp>
#include <Lattice/Kernel/Visibility.hpp>
#include <Lattice/Kernel/ServiceAPI.hpp>

namespace Lattice {

class NodeSystem;

class LATTICE_API NodeOps {
    NodeSystem& nodeSystem_;

public:
    NodeOps(NodeSystem& nodeSystem)
        : nodeSystem_(nodeSystem) {}

    void onRetire(NodeId id, std::function<void(void*)> callback);
    void readyToDestroy(NodeId id, std::function<bool(const void*)> callback);
    void bindService(NodeId id, std::unique_ptr<ServiceRunner> service);
    ServiceRunner* service(NodeId id) const;
    std::vector<NodeId> services(NodeId root) const;
    void maintain();

    // configure: repeatable wiring of composed branches with the current tree.
    /// Mark configure branches affected by a change at `changed` (its ancestors and
    /// branches that looked it up) to be reconfigured.
    void invalidate(NodeId changed);
    /// Composition of `id` is running: its configure waits until compose finishes.
    void beginCompose(NodeId id);
    /// Compose of `id` finished: run its configure now (if any).
    void endCompose(NodeId id);
    /// Compose of `id` failed: forget it without configuring.
    void abortCompose(NodeId id) noexcept;
    /// Run configure of every invalidated branch until none is left.
    void reconfigure();
    /// Remember that configure of `observer` looked up `observed` outside its subtree.
    void observe(NodeId observer, NodeId observed);
    void retireBranch(NodeId id);
    size_t collectRetired();
    void destroyBranch(NodeId id);
    void clearContents(NodeId id);
    /// Ask the node's own object to stop (once); true when it is ready to be destroyed.
    bool retireObject(NodeId id);

    void dumpTree(NodeId id, NodeId highlighted = InvalidNodeId) const;
    void dumpContext() const;

    NodeId resolvePath(NodeId from, std::string_view path) const;
    std::string stringPath(NodeId id) const;

    NodeId root(NodeId id) const;
    bool isUnder(NodeId id, NodeId ancestor) const;

    std::vector<NodeId> collectTree(NodeId id) const;
    std::vector<NodeId> collectRemovalClosure(NodeId id) const;

private:
    struct Lifecycle {
        std::function<void(void*)> retire;
        std::function<bool(const void*)> ready;
        std::unique_ptr<ServiceRunner> service;
        bool retireCalled = false;
    };
    void beginRetire(NodeId id);
    bool isReadyToDestroy(NodeId id) const;
    bool configurable(NodeId id) const;
    void configure(NodeId id);

    std::vector<Lifecycle> lifecycle_;
    std::vector<NodeId> dirty_;
    std::vector<NodeId> composing_;
    std::vector<NodeId> configured_;
    std::vector<std::pair<NodeId, NodeId>> observations_;   // observer, observed
    bool reconfiguring_ = false;
    std::vector<std::vector<NodeId>> retiring_;
    std::vector<NodeId> destroying_;
};

}
