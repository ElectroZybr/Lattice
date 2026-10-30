#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Lattice/kernel/Consts.hpp>
#include <Lattice/kernel/Visibility.hpp>
#include <Lattice/kernel/ServiceAPI.hpp>

namespace Lattice {

class NodeSystem;

class LT_API NodeOps {
    NodeSystem& nodeSystem_;

public:
    NodeOps(NodeSystem& nodeSystem)
        : nodeSystem_(nodeSystem) {}

    // Explicit: an exported class on Windows instantiates its implicit copy.
    NodeOps(const NodeOps&) = delete;
    NodeOps& operator=(const NodeOps&) = delete;

    void onRetire(NodeId id, std::function<void(void*)> callback);
    void onMaintain(NodeId id, std::function<void(void*)> callback);
    void readyToDestroy(NodeId id, std::function<bool(const void*)> callback);
    void bindService(NodeId id, std::unique_ptr<ServiceRunner> service);
    ServiceRunner* service(NodeId id) const;
    std::vector<NodeId> services(NodeId root) const;
    /// The node shows `status`, owned by its object: they live and go together.
    void bindStatus(NodeId id, const Status& status);
    /// The node's status: its object's, else its service's; nullptr — the node shows none.
    const Status* status(NodeId id) const;
    /// A service that reached its idle checkpoint and then stopped reaching it for this long is stalled.
    void setStallTimeout(std::chrono::milliseconds timeout) { stallTimeout_ = timeout; }
    /// Called in maintain, once per change of a node's state or detail (not per touch).
    uint64_t observeStatus(std::function<void(NodeId, const Status::Snapshot&)> callback);
    void unobserveStatus(uint64_t token);
    void maintain();

    /// Something was built: called at the end of a build and of maintain, outside any
    /// composition, when there are nodes or exports newer than at the previous call (a slot
    /// keeps its node when its implementation is replaced; the new one brings new exports).
    /// Builds made by a callback are reported after it returns.
    uint64_t observeBuilds(std::function<void()> callback);
    void unobserveBuilds(uint64_t token);
    void reportBuilds();

    // configure: repeatable wiring of composed branches with the current tree.
    /// Mark configure branches affected by a change at `changed` (its ancestors and
    /// branches that looked it up) to be reconfigured.
    void invalidate(NodeId changed);
    /// Composition of `id` is running: its configure waits until compose finishes.
    /// A composition is running (this one would be nested).
    bool composing() const noexcept { return !composing_.empty(); }
    void beginCompose(NodeId id);
    /// Compose of `id` finished: run its configure now (if any).
    void endCompose(NodeId id);
    /// Compose of `id` failed: forget it without configuring.
    void abortCompose(NodeId id) noexcept;
    /// Run configure of every invalidated branch until none is left.
    void reconfigure();
    /// Remember that configure of `observer` looked up `observed` outside its subtree.
    void observe(NodeId observer, NodeId observed);
    /// Remember that configure of `observer` read the Context role `role`.
    void observeRole(NodeId observer, RoleId role);
    void retireBranch(NodeId id);
    size_t collectRetired();
    void destroyBranch(NodeId id);
    void clearContents(NodeId id);
    /// Ask the node's own object to stop (once); true when it is ready to be destroyed.
    bool retireObject(NodeId id);

    NodeId resolvePath(NodeId from, std::string_view path) const;
    std::string stringPath(NodeId id) const;

    NodeId root(NodeId id) const;
    bool isUnder(NodeId id, NodeId ancestor) const;

    std::vector<NodeId> collectTree(NodeId id) const;
    std::vector<NodeId> collectRemovalClosure(NodeId id) const;

private:
    struct Lifecycle {
        std::function<void(void*)> retire;
        std::function<void(void*)> maintain;
        std::function<bool(const void*)> ready;
        std::unique_ptr<ServiceRunner> service;
        const Status* status = nullptr;
        uint64_t reportedChanges = 0;
        bool retireCalled = false;
    };
    void beginRetire(NodeId id);
    void maintainObjects();
    void maintainStatuses();
    bool isReadyToDestroy(NodeId id) const;
    bool configurable(NodeId id) const;
    void configure(NodeId id);

    std::vector<Lifecycle> lifecycle_;
    std::vector<NodeId> dirty_;
    std::vector<NodeId> composing_;
    std::vector<NodeId> configured_;
    std::vector<std::pair<NodeId, NodeId>> observations_;   // observer, observed
    std::vector<std::pair<NodeId, RoleId>> roleObservations_;   // observer, role
    bool reconfiguring_ = false;
    std::vector<std::pair<uint64_t, std::shared_ptr<std::function<void()>>>> buildObservers_;
    uint64_t nextBuildObserver_ = 1;
    uint64_t reportedGeneration_ = 0;
    uint32_t reportedExports_ = 0; // Exports::size()
    bool reporting_ = false;
    bool maintainingObjects_ = false;
    NodeId maintainingObject_ = InvalidNodeId;
    std::vector<std::pair<uint64_t, std::shared_ptr<std::function<void(NodeId, const Status::Snapshot&)>>>>
        statusObservers_;
    uint64_t nextStatusObserver_ = 1;
    std::chrono::milliseconds stallTimeout_{2000};
    std::vector<std::vector<NodeId>> retiring_;
    std::vector<NodeId> destroying_;
};

}
