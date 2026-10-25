#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include <Lattice/kernel/Consts.hpp>
#include <Lattice/kernel/Visibility.hpp>
#include <Lattice/tools/ObjectRegistry.hpp>


namespace Lattice {

class NodeRegistry;

struct Role {
    std::string name;
};

using RoleRegistry = ObjectRegistry<Role, RoleId, std::string>;

struct ContextEntry {
    RoleId role = InvalidRoleId;
    NodeId target = InvalidNodeId;
    std::vector<NodeId> candidates;
};

struct ContextScope {
    NodeId owner = InvalidNodeId;
    BlueprintId type = InvalidBlueprintId;
    std::vector<ContextEntry> roles;
};

using ContextRegistry = ObjectRegistry<ContextScope, ContextScopeId, NodeId>;

enum class ContextResolutionState : uint8_t {
    Missing,
    Resolved,
    Ambiguous
};

struct ContextResolution {
    ContextResolutionState state = ContextResolutionState::Missing;
    NodeId target = InvalidNodeId;
    std::span<const NodeId> candidates;
    /// The scope whose candidates were seen.
    ContextScopeId source = InvalidContextScopeId;
    /// Resolved as the only candidate, with no target selected anywhere on the way.
    bool implicit = false;

    explicit operator bool() const noexcept {
        return state == ContextResolutionState::Resolved;
    }
};

class LATTICE_API NodeContext {
    NodeRegistry& registry_;

    RoleRegistry roles_;
    ContextRegistry scopes_;
    ContextScopeId rootScope_ = InvalidContextScopeId;
    std::vector<ContextScopeId> activeScopes_;

public:
    explicit NodeContext(NodeRegistry& registry) : registry_(registry) {}

    // roles
    RoleId role(std::string_view name);
    RoleId findRole(std::string_view name) const;
    std::string_view roleName(RoleId id) const;

    // scopes
    ContextScopeId createScope(NodeId owner, BlueprintId type = InvalidBlueprintId);
    ContextScopeId parentScope(ContextScopeId scope) const;
    ContextScopeId findScope(NodeId owner) const;
    ContextScopeId nearestScope(NodeId owner) const;
    ContextScopeId active(BlueprintId type) const;

    void activate(ContextScopeId scope);
    void deactivate(ContextScopeId scope);
    bool isActive(ContextScopeId scope) const;
    void destroyScope(ContextScopeId scope);

    // candidates
    void addCandidate(ContextScopeId scope, RoleId role, NodeId target);
    void removeCandidate(ContextScopeId scope, RoleId role, NodeId target);
    void removeTarget(NodeId target);

    /**
     * Explicit focus/selection is separate from candidates. set() accepts only a
     * live candidate visible through this scope; reset() removes the local target.
     * Neither operation rewires object references or rebuilds dependent branches.
     */
    void set(ContextScopeId scope, RoleId role, NodeId target);
    void reset(ContextScopeId scope, RoleId role);

    // resolution
    // candidates is a non-owning view invalidated by context mutation.
    ContextResolution resolveInfo(ContextScopeId scope, RoleId role) const;
    ContextResolution resolveInfo(RoleId role) const;

    NodeId resolve(ContextScopeId scope, RoleId role) const;
    NodeId resolve(RoleId role) const;

    /// Resolve for use: the only candidate becomes the target of its scope, so a later
    /// candidate does not make the role ambiguous; the target's departure frees it again.
    ContextResolution use(ContextScopeId scope, RoleId role);

    // changes
    /// Call `callback` lazily (in maintain) after any change of `role`, while `owner`
    /// with this generation is alive and active. Dead owners are dropped.
    void observe(RoleId role, NodeId owner, uint64_t generation, std::function<void()> callback);
    /// Deliver the changes accumulated since the last call, once per changed role.
    void notifyChanges();
    /// Roles changed since the last notifyChanges().
    std::span<const RoleId> changedRoles() const noexcept { return changed_; }

    // helpers
    ContextScopeId root() const noexcept { return rootScope_; }
    size_t scopeCount() const noexcept { return scopes_.size(); }
    const ContextScope* scope(ContextScopeId id) const { return scopes_.get(id); }
    std::span<const ContextScopeId> activeScopes() const noexcept { return activeScopes_; }
    size_t roleCount() const noexcept { return roles_.size(); }
    bool hasRole(RoleId id) const { return roles_.get(id) != nullptr; }

private:
    struct Observation {
        RoleId role = InvalidRoleId;
        NodeId owner = InvalidNodeId;
        uint64_t generation = 0;
        std::function<void()> callback;
    };
    std::vector<RoleId> changed_;
    std::vector<Observation> observers_;

    void changed(RoleId role);
    ContextEntry* findEntry(ContextScopeId scope, RoleId role);
    const ContextEntry* findEntry(ContextScopeId scope, RoleId role) const;
    ContextEntry& requireEntry(ContextScopeId scope, RoleId role);
};

}
