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
    void retireBranch(NodeId id);
    size_t collectRetired();
    void destroyBranch(NodeId id);
    void clearContents(NodeId id);

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
    std::vector<Lifecycle> lifecycle_;
    std::vector<std::vector<NodeId>> retiring_;
    std::vector<NodeId> destroying_;
};

}
