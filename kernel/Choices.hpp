#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Lattice/kernel/Consts.hpp>
#include <Lattice/kernel/Exports.hpp>
#include <Lattice/kernel/Visibility.hpp>
#include <Lattice/tools/Exception.hpp>

namespace Lattice {

class NodeSystem;

/**
 * Someone needs what is not chosen: an empty slot with several implementations, an
 * ambiguous Context role. The outermost composition catches it: the branch waits for
 * the choice (Choices::pending) and is composed again once it is made.
 */
class LATTICE_API ChoiceRequired : public ExceptionBase {
public:
    ChoiceRequired(NodeId owner, std::string name, ExportId param, std::vector<std::string> options);
    ~ChoiceRequired() override;

    NodeId owner;                      // the node whose param makes the choice
    std::string name;                  // the param's name
    ExportId param;
    std::vector<std::string> options;
};

/// A choice a pending branch waits for, as tools show it.
struct PendingChoice {
    NodeId branch = InvalidNodeId;
    ExportId param = InvalidExportId;   // write it to make the choice
    std::string name;
    std::vector<std::string> options;
};

/**
 * Choice params: `device = gpu ∈ {cpu, gpu}` for a Context role, `renderer = Hardware ∈
 * {Hardware, Software}` for a slot. A param keeps nothing: it reads, writes and lists its
 * source — the scope's resolution and candidates, the slot's implementation and the
 * implementations of its API.
 *
 * A focus point (focus<T>() in compose) always has a choice; a role gets one where its
 * scope holds two candidates or more. A slot has one on its parent, under the slot's name.
 * A slot with a single implementation fills itself on first need (ref(), the end of a
 * composition, maintain); with several it waits for an explicit choice.
 */
class LATTICE_API Choices {
    struct Point {
        ContextScopeId scope = InvalidContextScopeId;
        RoleId role = InvalidRoleId;
        NodeId owner = InvalidNodeId;
        ExportId param = InvalidExportId;
    };

    struct SlotPoint {
        NodeId slot = InvalidNodeId;
        uint64_t generation = 0;
        BlueprintId api = InvalidBlueprintId;
        std::string name;
        ExportId param = InvalidExportId;
        /// Asked for while the old implementation retires; maintain() retries it.
        BlueprintId wanted = InvalidBlueprintId;
    };

    /// A branch whose composition needed a choice: its node waits, empty.
    struct Pending {
        NodeId branch = InvalidNodeId;
        uint64_t generation = 0;
        BlueprintId implementation = InvalidBlueprintId;
        PendingChoice choice;
        /// Name of the choice relative to the branch (`view.renderer`); empty when the
        /// choice is made outside it.
        std::string relative;
        ExportId placeholder = InvalidExportId;
    };
    /// A choice made for a pending branch before the param it belongs to exists again.
    struct Preselection {
        NodeId branch = InvalidNodeId;
        uint64_t generation = 0;
        std::string relative;
        std::string value;
    };

    NodeSystem& nodes_;
    std::vector<std::unique_ptr<Point>> points_;
    std::vector<std::unique_ptr<SlotPoint>> slots_;
    std::vector<std::unique_ptr<Pending>> pending_;
    std::vector<Preselection> preselected_;
    bool retry_ = false;
    size_t blueprints_ = 0;

    void preselect(NodeId owner, std::string_view name, ExportId param);
    void retryPending();

    bool alive(const Point& point) const;
    bool alive(const SlotPoint& point) const;
    std::string label(NodeId candidate, std::span<const NodeId> candidates) const;
    std::vector<std::string> labels(const Point& point) const;
    std::string implementationLabel(BlueprintId implementation, BlueprintId api) const;
    std::vector<std::string> implementationLabels(BlueprintId api) const;
    BlueprintId implementation(BlueprintId api, std::string_view name) const;
    SlotPoint* slotPoint(NodeId slot);
    bool empty(const SlotPoint& point) const;
    ParamResult choose(SlotPoint& point, std::string_view name);

public:
    explicit Choices(NodeSystem& nodes) : nodes_(nodes) {}

    /// A focus point: the scope chooses this role.
    void declare(ContextScopeId scope, RoleId role);
    /// A candidate was added: a choice once the scope holds two.
    void offer(ContextScopeId scope, RoleId role);
    /// The choice param of a scope's role, if it has one.
    ExportId param(ContextScopeId scope, RoleId role) const;
    /// A new slot: its choice param on the parent.
    void declareSlot(NodeId slot);
    /// Select a slot's implementation by name (`Hardware` or `Render::Hardware`). False
    /// while the old one retires: maintain() finishes the replacement. Error for a name
    /// that is no implementation of the slot's API.
    bool select(NodeId slot, std::string_view name);
    /// Fill an empty slot whose API has exactly one implementation (any other node: nothing).
    void fill(NodeId slot);
    /// Fill every such slot.
    void fill();
    /// Retry replacements asked for while their slots retired, compose pending branches
    /// whose choice may have been made, then fill().
    void maintain();

    /// Throw ChoiceRequired for an empty slot (an error when its API has no implementation).
    [[noreturn]] void requireSlot(NodeId slot);
    /// Throw ChoiceRequired for an ambiguous role, resolved through `source`.
    [[noreturn]] void requireFocus(ContextScopeId source, RoleId role);
    /// `view.renderer`: the name of `owner`'s param relative to `branch`; empty outside it.
    std::string relativeName(NodeId branch, NodeId owner, std::string_view name) const;
    /// The outermost composition of `branch` needed a choice: wait for it. `relative` is
    /// relativeName() taken before the branch was cleared.
    void defer(NodeId branch, BlueprintId implementation, const ChoiceRequired& required, std::string relative);
    /// Choices pending branches wait for.
    std::vector<PendingChoice> pending() const;
    /// `Graphics::Device` → `device`.
    static std::string choiceName(std::string_view role);
};

}
