#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include <Lattice/kernel/Consts.hpp>
#include <Lattice/kernel/Status.hpp>
#include <Lattice/kernel/Visibility.hpp>

namespace Lattice {

class NodeSystem;

/// Read-only view of what nodes show of their liveness: the status of a node's object
/// (ObjectBuild::status) or of its service. For tools (CLI, inspectors); how it looks is theirs.
class LT_API StatusView {
    NodeSystem* nodes_ = nullptr;

public:
    StatusView() = default;
    explicit StatusView(NodeSystem& nodes) : nodes_(&nodes) {}

    /// A snapshot; none for a node that shows no status.
    std::optional<Status::Snapshot> status(NodeId id) const;
    /// Called in maintain, once per change of a node's state or detail (not per touch).
    uint64_t observe(std::function<void(NodeId, const Status::Snapshot&)> callback) const;
    void unobserve(uint64_t token) const;
};

}
