#pragma once

#include <span>
#include <string_view>
#include <vector>

#include <Lattice/kernel/Consts.hpp>
#include <Lattice/kernel/Visibility.hpp>

class NodeBuild;

namespace Lattice {

class NodeFactory;
class NodeSystem;

class LT_API Builder {
public:
    // A nested class is exported on its own on Windows.
    class LT_API Batch {
    public:
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;
        Batch(Batch&& other) noexcept;
        Batch& operator=(Batch&&) = delete;
        ~Batch();

        NodeId add(
            NodeId parent,
            BlueprintId blueprint,
            std::string_view instance = DefaultInstanceName
        );

        void commit();
        void rollback() noexcept;

        std::span<const NodeId> branches() const noexcept { return branches_; }

    private:
        friend class Builder;

        explicit Batch(Builder& builder) : builder_(&builder) {}

        Builder* builder_ = nullptr;
        std::vector<NodeId> branches_;
        bool finished_ = false;
    };

    explicit Builder(NodeSystem& nodeSystem) : nodeSystem_(nodeSystem) {}

    Batch begin() { return Batch{*this}; }

    /**
     * @brief Build a new branch transactionally.
     *
     * Constructor arguments and query collections are snapshots. Context changes
     * affect subsequent builds, not existing objects. Rebuilding requires del(),
     * waiting until retirement removes the old branch, then add() again. A failed
     * new build is rolled back; it does not restore a previously retired branch.
     */
    NodeId add(
        NodeId parent,
        BlueprintId blueprint,
        std::string_view instance = DefaultInstanceName
    );

    /// Request retirement of a direct child and its dependent closure.
    /// The node can remain allocated until lifecycle readiness permits collection.
    void del(NodeId parent, NodeId child);

private:
    friend class Batch;
    friend class NodeFactory;

    ::NodeBuild node(NodeId id);

    NodeId create(
        NodeId parent,
        BlueprintId blueprint,
        std::string_view instance
    );

    NodeSystem& nodeSystem_;
};

}
