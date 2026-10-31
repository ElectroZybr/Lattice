#include <algorithm>
#include <cctype>
#include <format>

#include <Lattice/kernel/Choices.hpp>
#include <lattice-logger/Logger.hpp>
#include <Lattice/kernel/NodeSystem.hpp>

namespace Lattice {

namespace {
std::string joined(const std::vector<std::string>& items) {
    std::string result;
    for (const auto& item : items) result += (result.empty() ? "" : ", ") + item;
    return result;
}
}

ChoiceRequired::ChoiceRequired(NodeId owner, std::string name, ExportId param, std::vector<std::string> options)
    : ExceptionBase("Choices", std::format("choose '{}' ∈ {{{}}}", name, joined(options))),
      owner(owner), name(std::move(name)), param(param), options(std::move(options)) {}

ChoiceRequired::~ChoiceRequired() = default;

std::string Choices::choiceName(std::string_view role) {
    const auto at = role.rfind("::");
    std::string name(at == std::string_view::npos ? role : role.substr(at + 2));
    if (!name.empty()) name.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(name.front())));
    return name;
}

bool Choices::alive(const Point& point) const {
    const auto* scope = nodes_.context.scope(point.scope);
    const auto* entry = nodes_.exports.get(point.param);
    return scope && scope->owner == point.owner && nodes_.context.hasRole(point.role) &&
        entry && entry->owner == point.owner;
}

std::string Choices::label(NodeId candidate, std::span<const NodeId> candidates) const {
    const auto& node = nodes_.registry.require(candidate);
    const auto sameName = std::ranges::count_if(candidates, [&](NodeId other) {
        return nodes_.registry.require(other).name == node.name;
    });
    if (!node.name.empty() && sameName == 1) return node.name;
    const auto* type = nodes_.blueprints.get(node.type());
    return std::format("{}:{}", type ? type->shortName() : std::string("#"), node.name.empty()
        ? std::to_string(candidate) : node.name);
}

std::vector<std::string> Choices::labels(const Point& point) const {
    std::vector<std::string> result;
    if (!nodes_.context.scope(point.scope)) return result;
    const auto resolution = nodes_.context.resolveInfo(point.scope, point.role);
    for (const NodeId candidate : resolution.candidates) result.push_back(label(candidate, resolution.candidates));
    return result;
}

ExportId Choices::param(ContextScopeId scope, RoleId role) const {
    for (const auto& point : points_)
        if (point->scope == scope && point->role == role && alive(*point)) return point->param;
    return InvalidExportId;
}

void Choices::offer(ContextScopeId scope, RoleId role) {
    const auto* entry = nodes_.context.scope(scope);
    if (!entry) return;
    const auto found = std::ranges::find(entry->roles, role, &ContextEntry::role);
    if (found != entry->roles.end() && found->candidates.size() >= 2) declare(scope, role);
}

void Choices::declare(ContextScopeId scope, RoleId role) {
    std::erase_if(points_, [&](const auto& point) { return !alive(*point); });
    if (param(scope, role) != InvalidExportId) return;
    const auto* entry = nodes_.context.scope(scope);
    if (!entry) return;
    const NodeId owner = entry->owner;
    const auto* node = nodes_.registry.get(owner);
    if (!node || node->state == NodeState::Retiring) return;

    const std::string_view roleName = nodes_.context.roleName(role);
    // Export names and API keys share Context roles: the role key itself would make the
    // owner a candidate of its own role.
    std::string name = choiceName(roleName);
    if (nodes_.exports.find(owner, name) != InvalidExportId) name += "Focus";
    if (nodes_.exports.find(owner, name) != InvalidExportId) return;

    auto point = std::make_unique<Point>(Point{.scope = scope, .role = role, .owner = owner});
    Point* at = point.get();
    point->param = nodes_.exports.accessor<std::string>(owner, name,
        [this, at] {
            if (!nodes_.context.scope(at->scope)) return std::string{};
            const auto resolution = nodes_.context.resolveInfo(at->scope, at->role);
            return resolution ? label(resolution.target, resolution.candidates) : std::string{};
        },
        [this, at](const std::string& value) -> ParamResult {
            if (!nodes_.context.scope(at->scope))
                return reject(ParamError::Invalid, "the scope of this choice is gone");
            retry_ = true;
            if (value.empty()) {
                nodes_.context.reset(at->scope, at->role);
                return {};
            }
            const auto resolution = nodes_.context.resolveInfo(at->scope, at->role);
            for (const NodeId candidate : resolution.candidates)
                if (label(candidate, resolution.candidates) == value) {
                    nodes_.context.set(at->scope, at->role, candidate);
                    return {};
                }
            return reject(ParamError::Choice, std::format("'{}' is not a candidate", value), 0, value.size(),
                labels(*at));
        },
        [this, at] { return labels(*at); });
    // Visible by name like any local export of the owner.
    nodes_.context.addCandidate(nodes_.context.createScope(owner), nodes_.context.role(name), owner);
    const ExportId param = point->param;
    points_.push_back(std::move(point));
    preselect(owner, name, param);
}

// Slots

bool Choices::alive(const SlotPoint& point) const {
    const auto* node = nodes_.registry.get(point.slot);
    return node && node->kind == NodeKind::Slot && nodes_.registry.generation(point.slot) == point.generation;
}

bool Choices::empty(const SlotPoint& point) const {
    const auto& node = nodes_.registry.require(point.slot);
    return node.state == NodeState::Active && node.choice == InvalidBlueprintId && !node.object.building &&
        point.wanted == InvalidBlueprintId;
}

Choices::SlotPoint* Choices::slotPoint(NodeId slot) {
    for (auto& point : slots_)
        if (point->slot == slot && alive(*point)) return point.get();
    return nullptr;
}

std::string Choices::implementationLabel(BlueprintId implementation, BlueprintId api) const {
    const auto& blueprint = nodes_.blueprints.require(implementation);
    const auto shortName = blueprint.shortName();
    const auto all = nodes_.blueprints.implementations(api);
    const auto same = std::ranges::count_if(all, [&](BlueprintId other) {
        return nodes_.blueprints.require(other).shortName() == shortName;
    });
    return same == 1 ? shortName : blueprint.name;
}

std::vector<std::string> Choices::implementationLabels(BlueprintId api) const {
    std::vector<std::string> result;
    for (const BlueprintId id : nodes_.blueprints.implementations(api)) result.push_back(implementationLabel(id, api));
    return result;
}

BlueprintId Choices::implementation(BlueprintId api, std::string_view name) const {
    for (const BlueprintId id : nodes_.blueprints.implementations(api))
        if (nodes_.blueprints.require(id).name == name || implementationLabel(id, api) == name) return id;
    return InvalidBlueprintId;
}

ParamResult Choices::choose(SlotPoint& point, std::string_view name) {
    if (!alive(point) || nodes_.registry.require(point.slot).state != NodeState::Active)
        return reject(ParamError::Invalid, "the slot is gone or retiring");
    if (name.empty())
        return reject(ParamError::Invalid, "a slot is not emptied: choose an implementation",
            0, 0, implementationLabels(point.api));
    const BlueprintId chosen = implementation(point.api, name);
    if (chosen == InvalidBlueprintId)
        return reject(ParamError::Choice, std::format("'{}' is no implementation of '{}'", name,
            nodes_.blueprints.require(point.api).name), 0, name.size(), implementationLabels(point.api));
    retry_ = true;
    try {
        point.wanted = nodes_.factory.choice(point.slot, chosen) ? InvalidBlueprintId : chosen;
    } catch (const std::exception& error) {
        point.wanted = InvalidBlueprintId;
        return reject(ParamError::Invalid, error.what());
    }
    return {};
}

void Choices::declareSlot(NodeId slot) {
    std::erase_if(slots_, [&](const auto& point) { return !alive(*point); });
    if (slotPoint(slot)) return;
    const auto& node = nodes_.registry.require(slot);
    auto point = std::make_unique<SlotPoint>(SlotPoint{
        .slot = slot, .generation = nodes_.registry.generation(slot), .api = node.bp});
    SlotPoint* at = point.get();
    slots_.push_back(std::move(point));

    const NodeId owner = node.parent;
    if (owner == InvalidNodeId) return;
    const std::string name = node.name.empty() ? choiceName(nodes_.blueprints.require(node.bp).name) : node.name;
    if (nodes_.exports.find(owner, name) != InvalidExportId) {
        Logger::warning("Choices", "Slot '{}' has no choice param: its parent already exports '{}'", name, name);
        return;
    }
    at->name = name;
    at->param = nodes_.exports.accessor<std::string>(owner, name,
        [this, at] {
            if (!alive(*at)) return std::string{};
            const BlueprintId shown = at->wanted != InvalidBlueprintId ? at->wanted
                : nodes_.registry.require(at->slot).choice;
            return shown == InvalidBlueprintId ? std::string{} : implementationLabel(shown, at->api);
        },
        [this, at](const std::string& value) { return choose(*at, value); },
        [this, at] { return alive(*at) ? implementationLabels(at->api) : std::vector<std::string>{}; });
    nodes_.context.addCandidate(nodes_.context.createScope(owner), nodes_.context.role(name), owner);
    preselect(owner, name, at->param);
}

bool Choices::select(NodeId slot, std::string_view name) {
    auto* point = slotPoint(slot);
    if (!point) throw Exception<Choices>("Node #{} is not a live slot", slot);
    const auto result = choose(*point, name);
    if (!result) throw Exception<Choices>("{}", result.rejection().message);
    return point->wanted == InvalidBlueprintId;
}

void Choices::fill(NodeId slot) {
    auto* point = slotPoint(slot);
    if (!point || !empty(*point)) return;
    const auto all = nodes_.blueprints.implementations(point->api);
    if (all.size() == 1) nodes_.factory.choice(slot, all.front());
}

void Choices::fill() {
    std::vector<NodeId> waiting;
    for (const auto& point : slots_)
        if (alive(*point) && empty(*point)) waiting.push_back(point->slot);
    for (const NodeId slot : waiting) fill(slot);
}

void Choices::maintain() {
    if (!pending_.empty() && (retry_ || !nodes_.context.changedRoles().empty() ||
                              nodes_.blueprints.size() != blueprints_))
        retryPending();
    retry_ = false;
    blueprints_ = nodes_.blueprints.size();
    std::vector<std::pair<NodeId, BlueprintId>> retries;
    for (const auto& point : slots_)
        if (alive(*point) && point->wanted != InvalidBlueprintId) retries.emplace_back(point->slot, point->wanted);
    for (const auto [slot, wanted] : retries) {
        auto* point = slotPoint(slot);
        if (!point || point->wanted != wanted) continue;
        try {
            if (nodes_.factory.choice(slot, wanted)) point->wanted = InvalidBlueprintId;
        } catch (const std::exception& error) {
            point->wanted = InvalidBlueprintId;
            Logger::error("Choices", "Slot #{} keeps no implementation: {}", slot, error.what());
        }
    }
    fill();
}

// Pending branches

void Choices::requireSlot(NodeId slot) {
    auto* point = slotPoint(slot);
    if (!point) throw Exception<Choices>("Node #{} is not a live slot", slot);
    const auto options = implementationLabels(point->api);
    if (options.empty())
        throw Exception<Choices>("Slot '{}' is empty: no package implements '{}'",
            point->name, nodes_.blueprints.require(point->api).name);
    throw ChoiceRequired(nodes_.registry.require(slot).parent, point->name, point->param, options);
}

void Choices::requireFocus(ContextScopeId source, RoleId role) {
    declare(source, role);
    const ExportId id = param(source, role);
    const auto* entry = nodes_.exports.get(id);
    const auto* scope = nodes_.context.scope(source);
    std::vector<std::string> options;
    for (const auto& point : points_)
        if (point->scope == source && point->role == role) options = labels(*point);
    throw ChoiceRequired(scope ? scope->owner : InvalidNodeId,
        entry ? entry->name : std::string(nodes_.context.roleName(role)), id, std::move(options));
}

std::string Choices::relativeName(NodeId branch, NodeId owner, std::string_view name) const {
    std::vector<std::string_view> path;
    for (NodeId node = owner; node != InvalidNodeId; node = nodes_.registry.require(node).parent) {
        if (node == branch) {
            std::string result;
            for (auto it = path.rbegin(); it != path.rend(); ++it) result += std::string(*it) + ".";
            return result + std::string(name);
        }
        path.push_back(nodes_.registry.require(node).name);
    }
    return {};
}

void Choices::preselect(NodeId owner, std::string_view name, ExportId param) {
    for (auto it = preselected_.begin(); it != preselected_.end(); ++it) {
        if (!nodes_.registry.get(it->branch) || nodes_.registry.generation(it->branch) != it->generation) continue;
        if (relativeName(it->branch, owner, name) != it->relative) continue;
        const std::string value = std::move(it->value);
        preselected_.erase(it);
        // An explicit write: observers (the saved configuration) see the choice.
        if (const auto result = nodes_.exports.set(param, Value{value}); !result)
            Logger::warning("Choices", "'{}' keeps no choice: {}", name, result.rejection().message);
        return;
    }
}

void Choices::defer(NodeId branch, BlueprintId implementation, const ChoiceRequired& required,
    std::string relative) {
    std::erase_if(pending_, [&](const auto& entry) { return entry->branch == branch; });
    auto entry = std::make_unique<Pending>(Pending{
        .branch = branch,
        .generation = nodes_.registry.generation(branch),
        .implementation = implementation,
        .choice = {.branch = branch, .param = required.param, .name = required.name, .options = required.options},
        .relative = std::move(relative),
    });
    Pending* at = entry.get();
    // The choice belonged to a node the branch built: a placeholder on the branch keeps it.
    if (!at->relative.empty()) {
        at->placeholder = nodes_.exports.accessor<std::string>(branch, at->relative,
            [this, at] {
                for (const auto& chosen : preselected_)
                    if (chosen.branch == at->branch && chosen.relative == at->relative) return chosen.value;
                return std::string{};
            },
            [this, at](const std::string& value) -> ParamResult {
                if (std::ranges::find(at->choice.options, value) == at->choice.options.end())
                    return reject(ParamError::Choice, std::format("'{}' is not one of the choices", value),
                        0, value.size(), at->choice.options);
                std::erase_if(preselected_, [&](const auto& chosen) {
                    return chosen.branch == at->branch && chosen.relative == at->relative;
                });
                preselected_.push_back({at->branch, at->generation, at->relative, value});
                retry_ = true;
                return {};
            },
            [at] { return at->choice.options; });
        nodes_.context.addCandidate(nodes_.context.createScope(branch), nodes_.context.role(at->relative), branch);
        at->choice.param = at->placeholder;
        at->choice.name = at->relative;
    }
    Logger::warning("Choices", "'{}' waits for a choice: {} ∈ {{{}}} (#{})", nodes_.ops.stringPath(branch),
        at->choice.name, joined(at->choice.options), at->choice.param);
    pending_.push_back(std::move(entry));
}

std::vector<PendingChoice> Choices::pending() const {
    std::vector<PendingChoice> result;
    for (const auto& entry : pending_)
        if (nodes_.registry.get(entry->branch) && nodes_.registry.generation(entry->branch) == entry->generation)
            result.push_back(entry->choice);
    // Questions of objects: the same choices, answered the same way.
    for (const auto id : nodes_.exports.awaiting()) {
        const auto* entry = nodes_.exports.get(id);
        if (!entry) continue;
        PendingChoice question{entry->owner, id, entry->name, {}};
        for (const auto& option : nodes_.exports.rules(id).choices) question.options.push_back(option.toString());
        result.push_back(std::move(question));
    }
    return result;
}

void Choices::retryPending() {
    auto waiting = std::move(pending_);
    pending_.clear();
    for (auto& entry : waiting) {
        const auto* node = nodes_.registry.get(entry->branch);
        if (!node || node->state != NodeState::Active || nodes_.registry.generation(entry->branch) != entry->generation)
            continue;
        if (entry->placeholder != InvalidExportId) nodes_.exports.drop(entry->placeholder);
        try {
            nodes_.factory.recompose(entry->branch, entry->implementation);   // may defer again
        } catch (const std::exception& error) {
            Logger::error("Choices", "'{}' failed after its choice: {}", nodes_.ops.stringPath(entry->branch), error.what());
            nodes_.ops.destroyBranch(entry->branch);
        }
    }
    std::erase_if(preselected_, [&](const auto& chosen) {
        return !nodes_.registry.get(chosen.branch) || nodes_.registry.generation(chosen.branch) != chosen.generation;
    });
}

}
