#include <algorithm>

#include <Lattice/kernel/NodeContext.hpp>
#include <Lattice/kernel/NodeRegistry.hpp>

namespace Lattice {

RoleId NodeContext::role(std::string_view name) {
    RoleId id = roles_.find(std::string(name));

    if (id == InvalidRoleId)
        id = roles_.create(Role{std::string(name)}, std::string(name));

    return id;
}

RoleId NodeContext::findRole(std::string_view name) const {
    return roles_.find(std::string(name));
}

std::string_view NodeContext::roleName(RoleId id) const {
    return roles_.require(id).name;
}

ContextEntry* NodeContext::findEntry(ContextScopeId scopeId, RoleId roleId) {
    auto& scope = scopes_.require(scopeId);

    for (auto& entry : scope.roles)
        if (entry.role == roleId)
            return &entry;

    return nullptr;
}

const ContextEntry* NodeContext::findEntry(ContextScopeId scopeId, RoleId roleId) const {
    const auto& scope = scopes_.require(scopeId);

    for (const auto& entry : scope.roles)
        if (entry.role == roleId)
            return &entry;

    return nullptr;
}

ContextEntry& NodeContext::requireEntry(ContextScopeId scopeId, RoleId roleId) {
    roles_.require(roleId);

    if (auto* entry = findEntry(scopeId, roleId))
        return *entry;

    auto& scope = scopes_.require(scopeId);
    scope.roles.push_back(ContextEntry{.role = roleId});
    return scope.roles.back();
}

ContextScopeId NodeContext::createScope(NodeId owner, BlueprintId type) {
    if (const auto id = scopes_.find(owner); id != InvalidContextScopeId)
        return id;

    const ContextScopeId id = scopes_.create(ContextScope{
        .owner = owner,
        .type = type
    }, owner);

    if (registry_.require(owner).parent == InvalidNodeId)
        rootScope_ = id;

    return id;
}

ContextScopeId NodeContext::findScope(NodeId owner) const {
    return scopes_.find(owner);
}

ContextScopeId NodeContext::parentScope(ContextScopeId scope) const {
    NodeId owner = scopes_.require(scope).owner;

    for (NodeId parent = registry_.require(owner).parent;
         parent != InvalidNodeId;
         parent = registry_.require(parent).parent) {

        const ContextScopeId found = findScope(parent);

        if (found != InvalidContextScopeId)
            return found;
    }

    return InvalidContextScopeId;
}

ContextScopeId NodeContext::nearestScope(NodeId owner) const {
    for (NodeId id = owner; id != InvalidNodeId; id = registry_.require(id).parent) {
        const ContextScopeId scope = findScope(id);

        if (scope != InvalidContextScopeId)
            return scope;
    }

    return InvalidContextScopeId;
}

ContextScopeId NodeContext::active(BlueprintId type) const {
    for (ContextScopeId id : activeScopes_)
        if (scopes_.require(id).type == type)
            return id;

    return InvalidContextScopeId;
}

void NodeContext::activate(ContextScopeId scope) {
    scopes_.require(scope);

    if (scope == rootScope_)
        return;

    if (!isActive(scope))
        activeScopes_.push_back(scope);
}

void NodeContext::deactivate(ContextScopeId scope) {
    std::erase(activeScopes_, scope);
}

bool NodeContext::isActive(ContextScopeId scope) const {
    return std::ranges::find(activeScopes_, scope) != activeScopes_.end();
}

void NodeContext::destroyScope(ContextScopeId scope) {
    scopes_.require(scope);

    std::erase(activeScopes_, scope);

    if (rootScope_ == scope)
        rootScope_ = InvalidContextScopeId;

    for (const auto& entry : scopes_.require(scope).roles)
        changed(entry.role);
    scopes_.destroy(scope);
}

void NodeContext::addCandidate(ContextScopeId scopeId, RoleId roleId, NodeId target) {
    if (registry_.require(target).state == NodeState::Retiring)
        throw Exception<NodeContext>("Cannot add retiring node #{} as a context candidate", target);

    auto& entry = requireEntry(scopeId, roleId);

    if (std::ranges::find(entry.candidates, target) == entry.candidates.end()) {
        entry.candidates.push_back(target);
        changed(roleId);
    }
}

void NodeContext::removeCandidate(ContextScopeId scopeId, RoleId roleId, NodeId target) {
    auto& scope = scopes_.require(scopeId);

    for (auto it = scope.roles.begin(); it != scope.roles.end(); ++it) {
        if (it->role != roleId)
            continue;

        std::erase(it->candidates, target);

        if (it->target == target)
            it->target = InvalidNodeId;

        if (it->target == InvalidNodeId && it->candidates.empty())
            scope.roles.erase(it);

        changed(roleId);
        return;
    }
}

void NodeContext::set(ContextScopeId scopeId, RoleId roleId, NodeId target) {
    if (registry_.require(target).state == NodeState::Retiring)
        throw Exception<NodeContext>("Cannot focus retiring node #{}", target);

    const ContextResolution resolution = resolveInfo(scopeId, roleId);

    if (std::ranges::find(resolution.candidates, target) == resolution.candidates.end())
        throw Exception<NodeContext>(
            "Node #{} is not a candidate for role '{}'",
            target,
            roleName(roleId)
        );

    auto& entry = requireEntry(scopeId, roleId);
    if (entry.target != target) {
        entry.target = target;
        changed(roleId);
    }
}

void NodeContext::reset(ContextScopeId scopeId, RoleId roleId) {
    auto& scope = scopes_.require(scopeId);

    for (auto it = scope.roles.begin(); it != scope.roles.end(); ++it) {
        if (it->role != roleId)
            continue;

        if (it->target != InvalidNodeId)
            changed(roleId);
        it->target = InvalidNodeId;

        if (it->candidates.empty())
            scope.roles.erase(it);

        return;
    }
}

ContextResolution NodeContext::resolveInfo(ContextScopeId scopeId, RoleId roleId) const {
    roles_.require(roleId);

    NodeId target = InvalidNodeId;
    std::span<const NodeId> candidates;
    ContextScopeId source = InvalidContextScopeId;

    for (ContextScopeId scope = scopeId;
         scope != InvalidContextScopeId;
         scope = parentScope(scope)) {

        const auto* entry = findEntry(scope, roleId);

        if (!entry)
            continue;

        // Focus/explicit selection наследуется отдельно от candidates.
        if (target == InvalidNodeId && entry->target != InvalidNodeId)
            target = entry->target;

        // Ближайший scope с candidates определяет доступный набор.
        if (candidates.empty() && !entry->candidates.empty()) {
            candidates = entry->candidates;
            source = scope;
        }

        if (target != InvalidNodeId && !candidates.empty())
            break;
    }

    if (target != InvalidNodeId)
        return {
            .state = ContextResolutionState::Resolved,
            .target = target,
            .candidates = candidates,
            .source = source
        };

    if (candidates.empty())
        return {};

    if (candidates.size() == 1)
        return {
            .state = ContextResolutionState::Resolved,
            .target = candidates.front(),
            .candidates = candidates,
            .source = source,
            .implicit = true
        };

    return {
        .state = ContextResolutionState::Ambiguous,
        .target = InvalidNodeId,
        .candidates = candidates,
        .source = source
    };
}

ContextResolution NodeContext::use(ContextScopeId scopeId, RoleId roleId) {
    ContextResolution result = resolveInfo(scopeId, roleId);
    if (result.implicit) {
        // The entry exists (it holds the candidates): no reallocation under the span.
        requireEntry(result.source, roleId).target = result.target;
        result.implicit = false;
    }
    return result;
}

ContextResolution NodeContext::resolveInfo(RoleId roleId) const {
    ContextResolution result;

    if (rootScope_ != InvalidContextScopeId)
        result = resolveInfo(rootScope_, roleId);

    for (ContextScopeId active : activeScopes_) {
        // Ищем, есть ли вообще override этой роли между active и root.
        ContextScopeId scope = active;
        bool overrides = false;

        while (scope != InvalidContextScopeId && scope != rootScope_) {
            if (findEntry(scope, roleId)) {
                overrides = true;
                break;
            }

            scope = parentScope(scope);
        }

        if (overrides)
            result = resolveInfo(active, roleId);
    }

    return result;
}

NodeId NodeContext::resolve(ContextScopeId scope, RoleId role) const {
    const ContextResolution result = resolveInfo(scope, role);
    return result ? result.target : InvalidNodeId;
}

NodeId NodeContext::resolve(RoleId role) const {
    const ContextResolution result = resolveInfo(role);
    return result ? result.target : InvalidNodeId;
}

void NodeContext::removeTarget(NodeId target) {
    for (ContextScopeId id = 0; id < scopes_.size(); ++id) {
        auto* scope = scopes_.get(id);
        if (!scope)
            continue;

        for (auto it = scope->roles.begin(); it != scope->roles.end();) {
            if (std::erase(it->candidates, target) || it->target == target)
                changed(it->role);

            if (it->target == target)
                it->target = InvalidNodeId;

            if (it->target == InvalidNodeId && it->candidates.empty())
                it = scope->roles.erase(it);
            else
                ++it;
        }
    }
}

void NodeContext::changed(RoleId role) {
    if (std::ranges::find(changed_, role) == changed_.end())
        changed_.push_back(role);
}

void NodeContext::observe(RoleId role, NodeId owner, uint64_t generation, std::function<void()> callback) {
    observers_.push_back({role, owner, generation, std::move(callback)});
}

void NodeContext::notifyChanges() {
    if (changed_.empty())
        return;
    std::vector<RoleId> roles;
    roles.swap(changed_);
    const auto alive = [this](const Observation& observer) {
        const auto* node = registry_.get(observer.owner);
        return node && node->state == NodeState::Active &&
            registry_.generation(observer.owner) == observer.generation;
    };
    std::erase_if(observers_, [&](const Observation& observer) { return !alive(observer); });
    // Copy before calling: callbacks may observe or change Context again.
    std::vector<std::function<void()>> calls;
    for (const auto& observer : observers_)
        if (std::ranges::find(roles, observer.role) != roles.end())
            calls.push_back(observer.callback);
    for (auto& call : calls)
        call();
}

}
