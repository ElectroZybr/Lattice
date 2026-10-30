#pragma once

#include <algorithm>
#include <iterator>
#include <optional>
#include <memory>
#include <functional>
#include <tuple>

#include <Lattice/kernel/NodeFactory.hpp>
#include <Lattice/kernel/NodeHandlers.hpp>
#include <Lattice/kernel/Blueprints.hpp>
#include <Lattice/kernel/ObjectBlueprint.hpp>
#include <Lattice/tools/Exception.hpp>
#include <Lattice/kernel/NodeQuery.hpp>
#include <Lattice/kernel/NodeSystem.hpp>
#include <Lattice/kernel/CompositionArguments.hpp>
#include <Lattice/kernel/TreeView.hpp>
#include <Lattice/tools/TypeName.hpp>
#include <Lattice/kernel/Visibility.hpp>
#include <Lattice/kernel/ServiceAPI.hpp>


// global types

class ExportsView {
    Lattice::NodeSystem* nodeSystem_ = nullptr;

public:
    class AvailableExports {
    public:
        class Iterator {
        public:
            using iterator_category = std::input_iterator_tag;
            using value_type = Lattice::VisibleExport;
            using difference_type = std::ptrdiff_t;
            using pointer = const value_type*;
            using reference = const value_type&;

            Iterator() = default;

            reference operator*() const noexcept { return current_; }
            pointer operator->() const noexcept { return &current_; }

            Iterator& operator++() {
                advance();
                return *this;
            }

            Iterator operator++(int) {
                Iterator previous = *this;
                ++*this;
                return previous;
            }

            friend bool operator==(const Iterator& iterator, std::default_sentinel_t) noexcept {
                return iterator.current_.role == Lattice::InvalidRoleId;
            }

        private:
            friend class AvailableExports;

            Iterator(Lattice::NodeSystem* nodeSystem, Lattice::NodeId from)
                : nodeSystem_(nodeSystem),
                  scope_(nodeSystem ? nodeSystem->context.nearestScope(from) : Lattice::InvalidContextScopeId),
                  rootScope_(nodeSystem ? nodeSystem->context.root() : Lattice::InvalidContextScopeId) {
                advance();
            }

            void advance() {
                current_ = {};
                if (!nodeSystem_)
                    return;

                while (nextRole_ < nodeSystem_->context.roleCount()) {
                    const Lattice::RoleId role = nextRole_++;
                    if (!nodeSystem_->context.hasRole(role))
                        continue;

                    const Lattice::ContextResolution resolution =
                        scope_ == Lattice::InvalidContextScopeId
                            ? nodeSystem_->context.resolveInfo(role)
                            : nodeSystem_->context.resolveInfo(scope_, role);
                    if (resolution.state == Lattice::ContextResolutionState::Missing)
                        continue;

                    const Lattice::ContextResolution globalResolution =
                        rootScope_ == Lattice::InvalidContextScopeId
                            ? Lattice::ContextResolution{}
                            : nodeSystem_->context.resolveInfo(rootScope_, role);

                    Lattice::ExportId exportId = Lattice::InvalidExportId;
                    if (resolution.state == Lattice::ContextResolutionState::Resolved) {
                        exportId = nodeSystem_->exports.find(
                            resolution.target,
                            nodeSystem_->context.roleName(role)
                        );
                        if (exportId == Lattice::InvalidExportId)
                            continue;
                    }

                    current_ = {
                        .role = role,
                        .owner = resolution.target,
                        .exportId = exportId,
                        .scope = scope_,
                        .state = resolution.state,
                        .global = (
                            resolution.state == Lattice::ContextResolutionState::Resolved &&
                            globalResolution.state == Lattice::ContextResolutionState::Resolved &&
                            resolution.target == globalResolution.target
                        ) || (
                            resolution.state == Lattice::ContextResolutionState::Ambiguous &&
                            globalResolution.state == Lattice::ContextResolutionState::Ambiguous &&
                            std::ranges::equal(resolution.candidates, globalResolution.candidates)
                        )
                    };
                    return;
                }
            }

            Lattice::NodeSystem* nodeSystem_ = nullptr;
            Lattice::ContextScopeId scope_ = Lattice::InvalidContextScopeId;
            Lattice::ContextScopeId rootScope_ = Lattice::InvalidContextScopeId;
            Lattice::RoleId nextRole_ = 0;
            Lattice::VisibleExport current_;
        };

        Iterator begin() const { return Iterator{nodeSystem_, from_}; }
        std::default_sentinel_t end() const noexcept { return {}; }

    private:
        friend class ExportsView;

        AvailableExports(Lattice::NodeSystem* nodeSystem, Lattice::NodeId from)
            : nodeSystem_(nodeSystem), from_(from) {}

        Lattice::NodeSystem* nodeSystem_ = nullptr;
        Lattice::NodeId from_ = Lattice::InvalidNodeId;
    };

    ExportsView() = default;
    explicit ExportsView(Lattice::NodeSystem& nodeSystem) : nodeSystem_(&nodeSystem) {}

    Lattice::ParamResult set(const Lattice::ResolvedExport& target, const Lattice::Value& value) const {
        return nodeSystem_->exports.set(target.id, value);
    }

    /// Choices that pending branches wait for: write `param` to make one.
    std::vector<Lattice::PendingChoice> pending() const {
        return nodeSystem_->choices.pending();
    }

    /// Rules of a param (range, choices, length, own setter); empty when it has none.
    Lattice::ParamRules rules(Lattice::ExportId id) const {
        return nodeSystem_->exports.rules(id);
    }

    // Removes an explicit edit from observers; does not restore object state.
    void reset(const Lattice::ResolvedExport& target) const {
        nodeSystem_->exports.reset(target.id);
    }

    bool exists() const noexcept { return nodeSystem_ != nullptr; }
    explicit operator bool() const noexcept { return exists(); }

    Lattice::RoleId role(std::string_view name) const {
        return nodeSystem_->context.role(name);
    }

    Lattice::ResolvedExport resolve(Lattice::RoleId role) const {
        return resolveOwner(role, nodeSystem_->context.resolve(role));
    }

    Lattice::ResolvedExport resolve(Lattice::RoleId role, Lattice::NodeId from) const {
        const auto scope = nodeSystem_->context.nearestScope(from);
        const Lattice::NodeId owner = scope == Lattice::InvalidContextScopeId
            ? nodeSystem_->context.resolve(role)
            : nodeSystem_->context.resolve(scope, role);
        return resolveOwner(role, owner);
    }

    /// By id alone: an id names one export, no Context role is involved.
    Lattice::ResolvedExport resolveExport(Lattice::ExportId id) const {
        return nodeSystem_->exports.get(id) ? nodeSystem_->exports.resolve(id) : Lattice::ResolvedExport{};
    }

    Lattice::ResolvedExport resolveExport(Lattice::ExportId id, Lattice::NodeId from) const {
        const Lattice::Export* entry = nodeSystem_->exports.get(id);
        if (!entry)
            return {};

        const Lattice::RoleId role = nodeSystem_->context.findRole(entry->name);
        if (role == Lattice::InvalidRoleId)
            return {};

        const auto scope = nodeSystem_->context.nearestScope(from);
        const Lattice::NodeId owner = scope == Lattice::InvalidContextScopeId
            ? nodeSystem_->context.resolve(role)
            : nodeSystem_->context.resolve(scope, role);
        return owner == entry->owner ? nodeSystem_->exports.resolve(id) : Lattice::ResolvedExport{};
    }

    AvailableExports available(Lattice::NodeId from) const {
        return AvailableExports{nodeSystem_, from};
    }

    /// Exports owned by one node; for listing a subtree's params and actions.
    std::vector<Lattice::ExportId> owned(Lattice::NodeId node) const {
        return nodeSystem_->exports.owned(node);
    }

    std::optional<Lattice::ExportKind> kind(Lattice::ExportId id) const {
        const Lattice::Export* entry = nodeSystem_->exports.get(id);
        return entry ? std::optional{entry->kind} : std::nullopt;
    }

    std::optional<Lattice::Value> value(Lattice::ExportId id) const {
        const Lattice::ResolvedExport resolved = nodeSystem_->exports.resolve(id);
        return resolved.get ? std::optional{resolved.get(resolved.object)} : std::nullopt;
    }

    std::span<const Lattice::Value> argumentTypes(Lattice::ExportId id) const {
        return nodeSystem_->exports.resolve(id).argumentTypes;
    }

    size_t requiredArguments(Lattice::ExportId id) const {
        return nodeSystem_->exports.resolve(id).requiredArguments;
    }

    /// Whether the export is visible by name from the root scope (declared global).
    bool global(Lattice::ExportId id) const {
        const Lattice::Export* entry = nodeSystem_->exports.get(id);
        if (!entry)
            return false;
        const Lattice::RoleId role = nodeSystem_->context.findRole(entry->name);
        if (role == Lattice::InvalidRoleId)
            return false;
        const auto resolution = nodeSystem_->context.resolveInfo(nodeSystem_->context.root(), role);
        return std::ranges::find(resolution.candidates, entry->owner) != resolution.candidates.end();
    }

    std::string_view name(const Lattice::VisibleExport& entry) const {
        return nodeSystem_->context.roleName(entry.role);
    }

    std::string_view name(Lattice::ExportId id) const {
        const Lattice::Export* entry = nodeSystem_->exports.get(id);
        return entry ? std::string_view{entry->name} : std::string_view{};
    }

    std::optional<Lattice::ExportKind> kind(const Lattice::VisibleExport& entry) const {
        const Lattice::Export* exportEntry = nodeSystem_->exports.get(entry.exportId);
        if (!exportEntry)
            return std::nullopt;
        return exportEntry->kind;
    }

    std::optional<Lattice::Value> value(const Lattice::VisibleExport& entry) const {
        if (entry.exportId == Lattice::InvalidExportId)
            return std::nullopt;
        const Lattice::ResolvedExport resolved = nodeSystem_->exports.resolve(entry.exportId);
        if (!resolved.get)
            return std::nullopt;
        return resolved.get(resolved.object);
    }

    std::span<const Lattice::Value> argumentTypes(const Lattice::VisibleExport& entry) const {
        if (entry.exportId == Lattice::InvalidExportId)
            return {};
        return nodeSystem_->exports.resolve(entry.exportId).argumentTypes;
    }

    size_t requiredArguments(const Lattice::VisibleExport& entry) const {
        if (entry.exportId == Lattice::InvalidExportId)
            return 0;
        return nodeSystem_->exports.resolve(entry.exportId).requiredArguments;
    }

    std::span<const Lattice::NodeId> candidates(const Lattice::VisibleExport& entry) const {
        const Lattice::ContextResolution resolution =
            entry.scope == Lattice::InvalidContextScopeId
                ? nodeSystem_->context.resolveInfo(entry.role)
                : nodeSystem_->context.resolveInfo(entry.scope, entry.role);
        return resolution.candidates;
    }

    /// The param that chooses among an ambiguous entry's candidates, if there is one.
    Lattice::ExportId choice(const Lattice::VisibleExport& entry) const {
        const Lattice::ContextResolution resolution =
            entry.scope == Lattice::InvalidContextScopeId
                ? nodeSystem_->context.resolveInfo(entry.role)
                : nodeSystem_->context.resolveInfo(entry.scope, entry.role);
        return nodeSystem_->choices.param(resolution.source, entry.role);
    }

private:
    Lattice::ResolvedExport resolveOwner(
        Lattice::RoleId role,
        Lattice::NodeId owner
    ) const {
        if (owner == Lattice::InvalidNodeId) return {};

        const auto name = nodeSystem_->context.roleName(role);
        const Lattice::ExportId id = nodeSystem_->exports.find(owner, name);
        return id == Lattice::InvalidExportId ? Lattice::ResolvedExport{} : nodeSystem_->exports.resolve(id);
    }
};


template<class T>
class MountBuild {
    Lattice::NodeSystem* nodeSystem_ = nullptr;
    Lattice::NodeId owner_ = Lattice::InvalidNodeId;
    Lattice::NodeId target_ = Lattice::InvalidNodeId;
    uint64_t ownerGeneration_ = 0;
    uint64_t targetGeneration_ = 0;

public:
    MountBuild(Lattice::NodeSystem& nodeSystem, Lattice::NodeId owner, Lattice::NodeId target)
        : nodeSystem_(&nodeSystem), owner_(owner), target_(target),
          ownerGeneration_(nodeSystem.registry.generation(owner)),
          targetGeneration_(nodeSystem.registry.generation(target)) {}

    MountBuild(const MountBuild&) = delete;
    MountBuild& operator=(const MountBuild&) = delete;

    MountBuild(MountBuild&& other) noexcept
        : nodeSystem_(std::exchange(other.nodeSystem_, nullptr)),
          owner_(std::exchange(other.owner_, Lattice::InvalidNodeId)),
          target_(std::exchange(other.target_, Lattice::InvalidNodeId)),
          ownerGeneration_(other.ownerGeneration_), targetGeneration_(other.targetGeneration_) {}
    MountBuild& operator=(MountBuild&&) = delete;

    Ref<T> ref() const {
        check();
        return Lattice::detail::reference<T>(*nodeSystem_, target_);
    }

private:
    void check() const {
        if (!nodeSystem_)
            throw Lattice::Exception<MountBuild>("Mount builder was moved from");
        const auto* owner = nodeSystem_->registry.get(owner_);
        const auto* target = nodeSystem_->registry.get(target_);
        if (!owner || !target || owner->state != Lattice::NodeState::Active ||
            target->state != Lattice::NodeState::Active ||
            nodeSystem_->registry.generation(owner_) != ownerGeneration_ ||
            nodeSystem_->registry.generation(target_) != targetGeneration_)
            throw Lattice::Exception<MountBuild>("Mount builder has expired or is retiring");
    }

public:
    Lattice::NodeId owner() const noexcept { return owner_; }
    Lattice::NodeId target() const noexcept { return target_; }
};


class NodeBuild {
    template<class> friend class Lattice::Slot;
    Lattice::NodeId id_;
    Lattice::NodeSystem& nodeSystem_;
    uint64_t generation_;

public:
    NodeBuild(Lattice::NodeId id, Lattice::NodeSystem& nodeSystem)
        : id_(id), nodeSystem_(nodeSystem), generation_(nodeSystem.registry.generation(id)) {}

    NodeBuild(const NodeBuild&) = delete;
    NodeBuild& operator=(const NodeBuild&) = delete;
    NodeBuild(NodeBuild&& other) noexcept
        : id_(std::exchange(other.id_, Lattice::InvalidNodeId)), nodeSystem_(other.nodeSystem_),
          generation_(std::exchange(other.generation_, 0)) {}
    NodeBuild& operator=(NodeBuild&&) = delete;

    /// Construct the composition's own object, retaining its package identity and children.
    template<class T, class... APIs, class... Args>
    Lattice::ObjectBuild<T> construct(Args&&... args) {
        check();
        const auto type = Lattice::detail::ensureObject<T, APIs...>(nodeSystem_.blueprints);
        auto arguments = std::forward_as_tuple(std::forward<Args>(args)...);
        struct Arguments {
            Lattice::NodeSystem& runtime;
            decltype(arguments)& values;
        } context{nodeSystem_, arguments};
        nodeSystem_.factory.construct(id_, type,
            [](void* storage, Lattice::NodeId consumer, void* opaque) {
                auto& context = *static_cast<Arguments*>(opaque);
                std::apply([&](auto&&... values) {
                    std::construct_at(static_cast<T*>(storage),
                        Lattice::detail::compositionArgument(
                            std::forward<decltype(values)>(values), context.runtime, consumer)...);
                }, std::move(context.values));
            }, &context);
        return Lattice::ObjectBuild<T>{Lattice::detail::reference<T>(nodeSystem_, id_)};
    }

    /// construct() and publish the branch's object as a Context candidate, like emplaceShared.
    template<class T, class... APIs, class... Args>
    Lattice::ObjectBuild<T> constructShared(Args&&... args) {
        auto object = construct<T, APIs...>(std::forward<Args>(args)...);
        nodeSystem_.factory.share(id_, nodeSystem_.blueprints.id<T>());
        (nodeSystem_.factory.share(id_, nodeSystem_.blueprints.id<APIs>()), ...);
        return object;
    }

    template<class T, class... APIs, class... Args>
    Lattice::ObjectBuild<T> emplace(std::string_view instance, Args&&... args) {
        check();
        const auto type = Lattice::detail::ensureObject<T, APIs...>(nodeSystem_.blueprints);
        auto arguments = std::forward_as_tuple(std::forward<Args>(args)...);
        struct Arguments {
            Lattice::NodeSystem& runtime;
            decltype(arguments)& values;
        } context{nodeSystem_, arguments};
        const auto node = nodeSystem_.factory.emplace(id_, type, instance,
            [](void* storage, Lattice::NodeId consumer, void* opaque) {
                auto& context = *static_cast<Arguments*>(opaque);
                std::apply([&](auto&&... values) {
                    std::construct_at(static_cast<T*>(storage),
                        Lattice::detail::compositionArgument(
                            std::forward<decltype(values)>(values), context.runtime, consumer)...);
                }, std::move(context.values));
            }, &context);
        return Lattice::ObjectBuild<T>{Ref<T>{static_cast<T*>(nodeSystem_.factory.resolve(node, type)),
            &nodeSystem_, node, nodeSystem_.registry.generation(node)}};
    }

    template<class T>
    Lattice::OptionalRef<T> find(std::string_view instance = Lattice::DefaultInstanceName) const {
        check();
        const auto type = nodeSystem_.blueprints.find<T>();
        if (type == Lattice::InvalidBlueprintId)
            return {nullptr, &nodeSystem_, Lattice::InvalidNodeId};
        const auto node = nodeSystem_.query.find(id_, type, instance);
        const auto ref = Lattice::detail::reference<T>(nodeSystem_, node);
        return {ref.get(), ref.runtime(), ref.id(), ref.generation()};
    }

    template<class T>
    Ref<T> require(std::string_view instance = Lattice::DefaultInstanceName) const {
        check();
        const auto api = nodeSystem_.blueprints.id<T>();
        // A required slot is needed now: one implementation fills it.
        if (const auto found = nodeSystem_.query.find(id_, api, instance); found != Lattice::InvalidNodeId) {
            nodeSystem_.choices.fill(found);
            const auto& node = nodeSystem_.registry.require(found);
            if (node.kind == Lattice::NodeKind::Slot && node.choice == Lattice::InvalidBlueprintId &&
                !node.object.building && node.state == Lattice::NodeState::Active)
                nodeSystem_.choices.requireSlot(found);
        }
        const auto node = nodeSystem_.query.require(id_, api, instance);
        return Lattice::detail::reference<T>(nodeSystem_, node);
    }

    /// Snapshot of the nearest ancestor whose own object provides T: the object a
    /// nested composition (a slot implementation, a viewport's controller) belongs to.
    template<class T>
    Ref<T> owner() const {
        check();
        const auto type = nodeSystem_.blueprints.find<T>();
        if (type != Lattice::InvalidBlueprintId)
            for (auto node = nodeSystem_.registry.require(id_).parent; node != Lattice::InvalidNodeId;
                 node = nodeSystem_.registry.require(node).parent) {
                const auto& current = nodeSystem_.registry.require(node);
                if (current.kind == Lattice::NodeKind::Object && current.object.ptr &&
                    nodeSystem_.query.resolve(node, type))
                    return Lattice::detail::reference<T>(nodeSystem_, node);
            }
        throw Lattice::Exception<NodeBuild>("No owner providing '{}' above node #{}", Lattice::typeKey<T>(), id_);
    }

    template<class T>
    Lattice::Selection<T> children() const {
        check();
        std::vector<Ref<T>> result;
        const auto type = nodeSystem_.blueprints.find<T>();
        if (type != Lattice::InvalidBlueprintId)
            for (auto child : nodeSystem_.registry.children(id_))
                if (auto* object = static_cast<T*>(nodeSystem_.query.resolve(child, type)))
                    result.push_back(Lattice::detail::reference<T>(nodeSystem_, child));
        return Lattice::Selection<T>{std::move(result)};
    }

    template<class T>
    Lattice::Selection<T> collect() const {
        check();
        std::vector<Ref<T>> result;
        const auto type = nodeSystem_.blueprints.find<T>();
        if (type != Lattice::InvalidBlueprintId)
            for (auto node : nodeSystem_.query.collect(id_, type))
                if (auto* object = static_cast<T*>(nodeSystem_.query.resolve(node, type)))
                    result.push_back(Lattice::detail::reference<T>(nodeSystem_, node));
        return Lattice::Selection<T>{std::move(result)};
    }

    template<class T, class... APIs, class... Args>
    Lattice::ObjectBuild<T> emplaceShared(std::string_view instance, Args&&... args) {
        check();
        auto object = emplace<T, APIs...>(instance, std::forward<Args>(args)...);
        nodeSystem_.factory.share(object.id(), nodeSystem_.blueprints.id<T>());
        (nodeSystem_.factory.share(object.id(), nodeSystem_.blueprints.id<APIs>()), ...);
        return object;
    }

    /// Nest another package's composition (registered by its id) as a child branch.
    /// A failure rolls back together with the enclosing composition.
    Lattice::NodeId compose(std::string_view composition, std::string_view instance = Lattice::DefaultInstanceName) {
        check();
        const auto type = nodeSystem_.blueprints.resolve(composition);
        if (type == Lattice::InvalidBlueprintId || !nodeSystem_.blueprints.require(type).meta.compose)
            throw Lattice::Exception<NodeBuild>("Unknown composition '{}'", composition);
        return nodeSystem_.factory.component(id_, type, instance);
    }

    // Return a temporary build capability for a structural subbranch.
    NodeBuild branch(std::string_view name) {
        check();
        return NodeBuild{nodeSystem_.factory.folder(id_, name), nodeSystem_};
    }

    template<typename API>
    Slot<API> addSlot(std::string_view instance = Lattice::DefaultInstanceName) {
        check();
        const Lattice::NodeId node = nodeSystem_.factory.slot(id_, Lattice::detail::ensureObject<API>(nodeSystem_.blueprints), instance);
        return Slot<API>{nodeSystem_.factory, nodeSystem_.query, nodeSystem_.blueprints, node};
    }
    
    /**
     * @brief Obtain the contextual role owned by this branch.
     * @param role Defaults to the registered API type key.
     *
     * An empty local scope inherits parent candidates/choices. Creating a Focus
     * does not resolve a target, acquire ownership, or create dependency edges.
     */
    template<class T>
    Focus<T> focus(std::string_view role = Lattice::typeKey<T>()) {
        check();
        const auto scope = nodeSystem_.context.createScope(id_);
        const auto roleId = nodeSystem_.context.role(role);
        nodeSystem_.choices.declare(scope, roleId);
        return Focus<T>{nodeSystem_, scope, roleId};
    }

    /// Focus this branch on a live, compatible Context candidate. No ownership change.
    template<class T, class U>
    void focus(Ref<U> target) {
        check();
        Lattice::detail::resolveHandle(target, nodeSystem_);
        const auto type = nodeSystem_.blueprints.find<T>();
        if (type == Lattice::InvalidBlueprintId || !nodeSystem_.query.resolve(target.id(), type))
            throw Lattice::Exception<NodeBuild>("Selected node does not provide '{}'", Lattice::typeKey<T>());
        const auto scope = nodeSystem_.context.createScope(id_);
        const auto role = nodeSystem_.context.role(Lattice::typeKey<T>());
        nodeSystem_.context.set(scope, role, target.id());
        nodeSystem_.choices.declare(scope, role);
    }

    template<class T>
    MountBuild<T> mount() {
        check();
        const auto api = nodeSystem_.blueprints.id<T>();
        if (const auto role = nodeSystem_.context.findRole(Lattice::typeKey<T>()); role != Lattice::InvalidRoleId)
            if (const auto resolution = nodeSystem_.context.use(nodeSystem_.context.nearestScope(id_), role);
                resolution.state == Lattice::ContextResolutionState::Ambiguous)
                nodeSystem_.choices.requireFocus(resolution.source, role);
        const auto target = nodeSystem_.query.shared(id_, api);
        nodeSystem_.choices.fill(target);
        nodeSystem_.dependencies.retain(id_, target);
        return MountBuild<T>{nodeSystem_, id_, target};
    }

    template<typename T>
    Lattice::ParamBuild<T> param(std::string_view name, T& value) {
        check();
        const auto id = nodeSystem_.exports.param(id_, name, value);
        registerLocalExport(name);
        return {nodeSystem_.exports, id};
    }

    template<typename T>
    Lattice::ParamBuild<T> globalParam(std::string_view name, T& value) {
        check();
        const auto id = nodeSystem_.exports.param(id_, name, value);
        registerGlobalExport(name);
        return {nodeSystem_.exports, id};
    }

    template<typename... Args, typename F>
    Lattice::ExportId action(std::string_view name, F&& callback) {
        check();
        const auto id = nodeSystem_.exports.action<Args...>(id_, name, std::forward<F>(callback));
        registerLocalExport(name);
        return id;
    }

    template<typename... Args, typename F>
    Lattice::ExportId globalAction(std::string_view name, F&& callback) {
        check();
        const auto id = nodeSystem_.exports.action<Args...>(id_, name, std::forward<F>(callback));
        registerGlobalExport(name);
        return id;
    }

    void alias(std::string_view name, Lattice::ExportId target) {
        check();
        nodeSystem_.exports.alias(id_, name, target);
        registerLocalExport(name);
    }

    void globalAlias(std::string_view name, Lattice::ExportId target) {
        check();
        nodeSystem_.exports.alias(id_, name, target);
        registerGlobalExport(name);
    }

    Lattice::TreeView tree() const { check(); return Lattice::TreeView{nodeSystem_}; }
    // Runtime views for infrastructure objects (routers, shells); pass to constructors.
    ExportsView exports() const { check(); return ExportsView{nodeSystem_}; }

    // helpers
    Lattice::NodeId id() const noexcept { return id_; }
    std::string_view name() const { check(); return nodeSystem_.registry.require(id_).name; }
    Lattice::NodeId parent() const { check(); return nodeSystem_.registry.require(id_).parent; }
    Lattice::NodeKind kind() const { check(); return nodeSystem_.registry.require(id_).kind; }
    Lattice::BlueprintId blueprint() const { check(); return nodeSystem_.registry.require(id_).bp; }
    Lattice::BlueprintId implementation() const { check(); return nodeSystem_.registry.require(id_).object.bp; }

private:
    void check() const {
        const auto* node = nodeSystem_.registry.get(id_);
        if (!node || node->state != Lattice::NodeState::Active ||
            nodeSystem_.registry.generation(id_) != generation_)
            throw Lattice::Exception<NodeBuild>("Builder is moved-from, expired or retiring");
    }

    void registerLocalExport(std::string_view name) {
        const Lattice::ContextScopeId scope = nodeSystem_.context.createScope(id_);
        nodeSystem_.context.addCandidate(scope, nodeSystem_.context.role(name), id_);
    }

    void registerGlobalExport(std::string_view name) {
        Lattice::NodeId root = id_;
        while (nodeSystem_.registry.require(root).parent != Lattice::InvalidNodeId)
            root = nodeSystem_.registry.require(root).parent;

        const Lattice::ContextScopeId scope = nodeSystem_.context.createScope(root);
        nodeSystem_.context.addCandidate(scope, nodeSystem_.context.role(name), id_);
    }
};

/**
 * @brief Wiring of a composed branch with the current tree.
 *
 * A package may export `extern "C" void configure(NodeConfigure root)` next to compose
 * (a slot variant passes it to choice). The runtime calls it after compose and again
 * in maintain when the branch subtree changes, a node it looked up retires, or a Context
 * role it read (focus/focusId) changes. It only queries and assigns plain values to objects: it never creates nodes.
 * Results are not dependencies: a retiring child is dropped by the next configure
 * (before it is destroyed) instead of retiring its consumer.
 */
template<class T>
struct ConfiguredNode;

class NodeConfigure {
    Lattice::NodeId id_ = Lattice::InvalidNodeId;
    Lattice::NodeSystem* nodeSystem_ = nullptr;
    Lattice::NodeId observer_ = Lattice::InvalidNodeId;

public:
    NodeConfigure(Lattice::NodeId id, Lattice::NodeSystem& nodeSystem, Lattice::NodeId observer)
        : id_(id), nodeSystem_(&nodeSystem), observer_(observer) {}

    Lattice::NodeId id() const noexcept { return id_; }

    template<class T>
    using Entry = ConfiguredNode<T>;

    /// Own object of this node (the branch object or a slot implementation).
    template<class T>
    T* self() const { return object<T>(id_); }

    template<class T>
    std::vector<T*> children() const { return objects(childNodes<T>()); }

    /// The subtree below this node (the node itself excluded).
    template<class T>
    std::vector<T*> collect() const { return objects(collectNodes<T>()); }

    template<class T>
    std::vector<Entry<T>> childNodes() const {
        std::vector<Entry<T>> result;
        for (Lattice::NodeId child : nodeSystem_->registry.children(id_))
            if (T* found = object<T>(child))
                result.push_back({found, at(child)});
        return result;
    }

    template<class T>
    std::vector<Entry<T>> collectNodes() const {
        std::vector<Entry<T>> result;
        const auto type = nodeSystem_->blueprints.find<T>();
        if (type == Lattice::InvalidBlueprintId)
            return result;
        for (Lattice::NodeId node : nodeSystem_->query.collect(id_, type))
            if (node != id_)
                if (T* found = object<T>(node))
                    result.push_back({found, at(node)});
        return result;
    }

    /// Environment lookup by name, as in composition; nullptr when absent.
    template<class T>
    T* find(std::string_view instance = Lattice::DefaultInstanceName) const {
        const auto type = nodeSystem_->blueprints.find<T>();
        if (type == Lattice::InvalidBlueprintId)
            return nullptr;
        return observed<T>(nodeSystem_->query.find(id_, type, instance));
    }

    template<class T>
    T& require(std::string_view instance = Lattice::DefaultInstanceName) const {
        if (T* found = find<T>(instance))
            return *found;
        throw Lattice::Exception<NodeConfigure>("'{}' named '{}' not found from node #{}",
            Lattice::typeKey<T>(), instance, id_);
    }

    /// Current Context choice for this node: nullptr when missing, error when ambiguous.
    template<class T>
    T* focus(std::string_view role = Lattice::typeKey<T>()) const {
        const auto roleId = nodeSystem_->context.role(role);
        nodeSystem_->ops.observeRole(observer_, roleId);
        const auto resolution = nodeSystem_->context.use(nodeSystem_->context.nearestScope(id_), roleId);
        if (resolution.state == Lattice::ContextResolutionState::Ambiguous)
            throw Lattice::Exception<NodeConfigure>("Context role '{}' is ambiguous ({} candidates)",
                role, resolution.candidates.size());
        if (resolution.state == Lattice::ContextResolutionState::Missing)
            return nullptr;
        return observed<T>(resolution.target);
    }

    /// Node of the current Context choice: InvalidNodeId when missing, error when ambiguous.
    template<class T>
    Lattice::NodeId focusId(std::string_view role = Lattice::typeKey<T>()) const {
        const auto roleId = nodeSystem_->context.role(role);
        nodeSystem_->ops.observeRole(observer_, roleId);
        const auto resolution = nodeSystem_->context.use(nodeSystem_->context.nearestScope(id_), roleId);
        if (resolution.state == Lattice::ContextResolutionState::Ambiguous)
            throw Lattice::Exception<NodeConfigure>("Context role '{}' is ambiguous ({} candidates)",
                role, resolution.candidates.size());
        if (resolution.state == Lattice::ContextResolutionState::Missing || !object<T>(resolution.target))
            return Lattice::InvalidNodeId;
        nodeSystem_->ops.observe(observer_, resolution.target);
        return resolution.target;
    }

    /// Nearest ancestor whose own object provides T.
    template<class T>
    T& owner() const {
        for (auto node = nodeSystem_->registry.require(id_).parent; node != Lattice::InvalidNodeId;
             node = nodeSystem_->registry.require(node).parent)
            if (object<T>(node))
                return *observed<T>(node);
        throw Lattice::Exception<NodeConfigure>("No owner providing '{}' above node #{}", Lattice::typeKey<T>(), id_);
    }

private:
    NodeConfigure at(Lattice::NodeId node) const { return {node, *nodeSystem_, observer_}; }

    /// The constructed, active object of `node` as T, or nullptr.
    template<class T>
    T* object(Lattice::NodeId node) const {
        const auto type = nodeSystem_->blueprints.find<T>();
        if (type == Lattice::InvalidBlueprintId || node == Lattice::InvalidNodeId)
            return nullptr;
        const auto target = nodeSystem_->query.objectNode(node);
        const auto* entry = target == Lattice::InvalidNodeId ? nullptr : nodeSystem_->registry.get(target);
        if (!entry || entry->state != Lattice::NodeState::Active || !entry->object.constructed ||
            nodeSystem_->registry.require(node).state != Lattice::NodeState::Active)
            return nullptr;
        return static_cast<T*>(nodeSystem_->query.resolve(node, type));
    }

    /// Lookup outside the subtree: its retirement must reconfigure the observer.
    template<class T>
    T* observed(Lattice::NodeId node) const {
        T* found = object<T>(node);
        if (found)
            nodeSystem_->ops.observe(observer_, node);
        return found;
    }

    template<class T>
    static std::vector<T*> objects(const std::vector<Entry<T>>& entries) {
        std::vector<T*> result;
        result.reserve(entries.size());
        for (const auto& entry : entries)
            result.push_back(entry.object);
        return result;
    }
};

/// An object of the tree together with the configure view of its node.
template<class T>
struct ConfiguredNode {
    T* object;
    NodeConfigure node;
};

namespace Lattice {

template<class T>
Slot<T>::Slot(NodeFactory& factory, NodeQuery&, Blueprints&, NodeId id)
    : runtime_(&factory.nodeSystem_), id_(id), generation_(runtime_->registry.generation(id)) {}

template<class T>
bool Slot<T>::valid() const noexcept {
    const auto* node = runtime_ ? runtime_->registry.get(id_) : nullptr;
    return node && node->kind == NodeKind::Slot && node->state == NodeState::Active &&
        runtime_->registry.generation(id_) == generation_;
}

template<class T>
T* Slot<T>::get() const {
    return valid() ? static_cast<T*>(runtime_->query.resolve(id_, runtime_->blueprints.id<T>())) : nullptr;
}

template<class T>
Ref<T> Slot<T>::ref() const {
    if (valid() && !get()) runtime_->choices.fill(id_);
    if (valid() && !get() && !runtime_->registry.require(id_).object.building &&
        runtime_->registry.require(id_).choice == InvalidBlueprintId)
        runtime_->choices.requireSlot(id_);
    if (!valid() || !get())
        throw Exception<Slot>("Slot '{}' is empty, retiring, or expired",
            runtime_ && runtime_->registry.get(id_) ? runtime_->registry.require(id_).name : std::string{});
    return detail::reference<T>(*runtime_, id_);
}

template<class T>
bool Slot<T>::select(std::string_view name) {
    if (!valid())
        throw Exception<Slot>("Slot has expired or is retiring");
    return runtime_->choices.select(id_, name);
}

template<class T>
template<auto Compose>
uint32_t Slot<T>::compose(::NodeBuild& branch) {
    auto result = Compose(branch);
    if (result.runtime() != &branch.nodeSystem_)
        throw Exception<Slot>("Composition must return an object from this runtime");
    using Object = std::remove_pointer_t<decltype(result.get())>;
    if constexpr (!std::is_same_v<Object, T>)
        detail::ensureObject<Object, T>(branch.nodeSystem_.blueprints);
    if constexpr (requires { result.ref(); })
        detail::resolveHandle(result.ref(), *result.runtime());
    else
        detail::resolveHandle(result, *result.runtime());
    return result.id();
}

template<class T>
Focus<T>::Focus(NodeSystem& runtime, ContextScopeId scope, RoleId role)
    : runtime_(&runtime), scope_(scope), role_(role) {
    const auto* context = runtime.context.scope(scope);
    if (!context)
        throw Exception<Focus>("Focus requires a live branch scope");
    owner_ = context->owner;
    ownerGeneration_ = runtime.registry.generation(owner_);
}

template<class T>
bool Focus<T>::valid() const noexcept {
    if (!runtime_ || !runtime_->context.hasRole(role_))
        return false;
    const auto* scope = runtime_->context.scope(scope_);
    const auto* owner = runtime_->registry.get(owner_);
    return scope && scope->owner == owner_ && owner && owner->state == NodeState::Active &&
        runtime_->registry.generation(owner_) == ownerGeneration_;
}

template<class T>
NodeId Focus<T>::id() const {
    if (!valid()) return InvalidNodeId;
    const auto resolution = runtime_->context.use(scope_, role_);
    return resolution ? resolution.target : InvalidNodeId;
}

template<class T>
T* Focus<T>::get() const {
    const auto target = id();
    const auto type = runtime_ ? runtime_->blueprints.find<T>() : InvalidBlueprintId;
    return target == InvalidNodeId || type == InvalidBlueprintId ? nullptr :
        static_cast<T*>(runtime_->query.resolve(target, type));
}

template<class T>
Ref<T> Focus<T>::ref() const {
    if (!valid())
        throw Exception<Focus>("Focus scope has expired or is retiring");
    const auto resolution = runtime_->context.use(scope_, role_);
    if (resolution.state == ContextResolutionState::Ambiguous)
        runtime_->choices.requireFocus(resolution.source, role_);
    if (resolution) runtime_->choices.fill(resolution.target);
    if (resolution.state == ContextResolutionState::Missing)
        throw Exception<Focus>("Context role '{}' is missing", runtime_->context.roleName(role_));
    auto* object = get();
    if (!object)
        throw Exception<Focus>("Context role '{}' has no available '{}' object",
            runtime_->context.roleName(role_), typeKey<T>());
    return detail::reference<T>(*runtime_, resolution.target);
}

template<class T>
template<class U>
void Focus<T>::set(const Ref<U>& target) {
    if (!valid())
        throw Exception<Focus>("Focus scope has expired or is retiring");
    detail::resolveHandle(target, *runtime_);
    const auto type = runtime_->blueprints.find<T>();
    if (type == InvalidBlueprintId || !runtime_->query.resolve(target.id(), type))
        throw Exception<Focus>("Selected node does not provide '{}'", typeKey<T>());
    // A branch may focus any compatible object it owns, not only visible candidates.
    if (runtime_->ops.isUnder(target.id(), owner_))
        runtime_->context.addCandidate(scope_, role_, target.id());
    runtime_->context.set(scope_, role_, target.id());
}

template<class T>
void Focus<T>::set(T* object) {
    if (!valid())
        throw Exception<Focus>("Focus scope has expired or is retiring");
    if (!object)
        throw Exception<Focus>("Focus on '{}' requires an object", typeKey<T>());
    const auto type = runtime_->blueprints.find<T>();
    if (type == InvalidBlueprintId)
        throw Exception<Focus>("'{}' has no objects", typeKey<T>());
    const auto matches = [&](NodeId node) {
        const auto* entry = runtime_->registry.get(node);
        return entry && entry->state == NodeState::Active && runtime_->query.resolve(node, type) == object;
    };
    auto nodes = runtime_->query.collect(owner_, type);
    const auto candidates = runtime_->context.resolveInfo(scope_, role_).candidates;
    nodes.insert(nodes.end(), candidates.begin(), candidates.end());
    for (const NodeId node : nodes)
        if (matches(node))
            return set(detail::reference<T>(*runtime_, node));
    throw Exception<Focus>("Object is not a '{}' this scope can focus", typeKey<T>());
}

template<class T>
template<class U, class F>
void Focus<T>::onChange(const Ref<U>& owner, F callback) const {
    if (!valid())
        throw Exception<Focus>("Focus scope has expired or is retiring");
    detail::resolveHandle(owner, *runtime_);
    static_assert(std::is_invocable_v<F&, U&, const Focus<T>&>, "callback(owner&, const Focus&)");
    runtime_->context.observe(role_, owner.id(), owner.generation(),
        [focus = *this, owner, callback = std::move(callback)]() mutable { callback(*owner, focus); });
}

template<class T>
void Focus<T>::reset() {
    if (!valid())
        throw Exception<Focus>("Focus scope has expired or is retiring");
    runtime_->context.reset(scope_, role_);
}

template<class T>
Ref<T> ObjectBuild<T>::ref() const {
    if (!this->runtime())
        throw Exception<ObjectBuild>("Object builder was moved from");
    detail::resolveHandle(static_cast<const Ref<T>&>(*this), *this->runtime());
    return static_cast<const Ref<T>&>(*this);
}

namespace detail {

template<class... Args>
inline constexpr bool leadingContext = false;
template<class... Rest>
inline constexpr bool leadingContext<ActionContext&, Rest...> = true;

// Register a method of a live object as an action owned by that object's node.
// A leading ActionContext& parameter receives the invocation context.
template<bool Global, class T, class Member, class... Args>
ExportId objectAction(Ref<T> object, std::string_view name, Member member) {
    ::NodeBuild node{object.id(), *object.runtime()};
    const auto add = [&]<class... Exported>(auto callback) {
        if constexpr (Global)
            return node.template globalAction<Exported...>(name, std::move(callback));
        else
            return node.template action<Exported...>(name, std::move(callback));
    };
    if constexpr (leadingContext<Args...>) {
        return [&]<class Context, class... Rest>(std::type_identity<void(Context, Rest...)>) {
            return add.template operator()<Rest...>([object, member](ActionContext& context, Rest... args) {
                return std::invoke(member, *object, context, std::forward<Rest>(args)...);
            });
        }(std::type_identity<void(Args...)>{});
    } else {
        return add.template operator()<Args...>([object, member](Args... args) {
            return std::invoke(member, *object, std::forward<Args>(args)...);
        });
    }
}

}

template<class T>
template<class Field, class Owner>
ParamBuild<Field> ObjectBuild<T>::param(std::string_view name, Field Owner::* member) {
    auto object = ref();
    return ::NodeBuild{object.id(), *object.runtime()}.param(name, (*object).*member);
}

template<class T>
template<class Field>
ParamBuild<Field> ObjectBuild<T>::param(std::string_view name, Field& value) {
    auto object = ref();
    return ::NodeBuild{object.id(), *object.runtime()}.param(name, value);
}

template<class T>
template<class Field, class Owner, class Setter>
ParamBuild<Field> ObjectBuild<T>::param(std::string_view name, Field Owner::* member, Setter&& setter) {
    using Apply = std::decay_t<Setter>;
    static_assert(std::is_invocable_v<Apply&, T&, const Field&>,
        "A param setter takes (T&, const Field&): a method of the object or a callable");
    static_assert(std::is_same_v<std::invoke_result_t<Apply&, T&, const Field&>, ParamResult>,
        "A param setter returns ParamResult: apply the value or refuse it");
    auto build = param(name, member);
    T* object = &*ref();
    build.setter([object, apply = Apply(std::forward<Setter>(setter))](const Field& value) mutable {
        return std::invoke(apply, *object, value);
    });
    return build;
}

template<class T>
template<class Queue>
void ObjectBuild<T>::executeOn(const Ref<Queue>& queue) {
    auto object = ref();
    const Ref<Queue> target = queue;
    detail::resolveHandle(target, *object.runtime());
    // The object's exports run through the queue: it must outlive them.
    object.runtime()->dependencies.retain(object.id(), target.id());
    Queue* commands = target.get();
    std::function<void(std::function<void()>)> post;
    if constexpr (requires(std::function<void()> work) { commands->post(std::move(work)); })
        post = [commands](std::function<void()> work) { commands->post(std::move(work)); };
    object.runtime()->exports.executeOn(object.id(), [commands](const std::function<void()>& work) {
        commands->call([&work] { work(); });
    }, std::move(post));
}

template<class T>
template<class Owner, class Result, class... Args>
ExportId ObjectBuild<T>::action(std::string_view name, Result (Owner::*member)(Args...)) {
    return detail::objectAction<false, T, decltype(member), Args...>(ref(), name, member);
}

template<class T>
template<class Owner, class Result, class... Args>
ExportId ObjectBuild<T>::action(std::string_view name, Result (Owner::*member)(Args...) const) {
    return detail::objectAction<false, T, decltype(member), Args...>(ref(), name, member);
}

template<class T>
template<class... Args, class F>
    requires (!std::is_member_function_pointer_v<std::decay_t<F>>)
ExportId ObjectBuild<T>::action(std::string_view name, F&& callback) {
    auto object = ref();
    ::NodeBuild node{object.id(), *object.runtime()};
    if constexpr (std::is_invocable_v<F&, T&, ActionContext&, Args...>)
        return node.template action<Args...>(name,
            [object, callback = std::forward<F>(callback)](ActionContext& context, Args... args) mutable {
                std::invoke(callback, *object, context, std::forward<Args>(args)...);
            });
    else {
        static_assert(std::is_invocable_v<F&, T&, Args...>,
            "Object action callback must accept (T&, [ActionContext&,] Args...)");
        return node.template action<Args...>(name,
            [object, callback = std::forward<F>(callback)](Args... args) mutable {
                std::invoke(callback, *object, std::forward<Args>(args)...);
            });
    }
}

template<class T>
template<class Owner, class Result, class... Args>
ExportId ObjectBuild<T>::globalAction(std::string_view name, Result (Owner::*member)(Args...)) {
    return detail::objectAction<true, T, decltype(member), Args...>(ref(), name, member);
}

template<class T>
template<class Owner, class Result, class... Args>
ExportId ObjectBuild<T>::globalAction(std::string_view name, Result (Owner::*member)(Args...) const) {
    return detail::objectAction<true, T, decltype(member), Args...>(ref(), name, member);
}

template<class T>
template<class F>
void ObjectBuild<T>::onMaintain(F&& callback) {
    static_assert(std::is_invocable_r_v<void, F&, T&>);
    auto object = ref();
    object.runtime()->ops.onMaintain(object.id(),
        [callback = std::forward<F>(callback)](void* ptr) mutable {
            std::invoke(callback, *static_cast<T*>(ptr));
        });
}

template<class T>
template<class F>
void ObjectBuild<T>::onRetire(F&& callback) {
    static_assert(std::is_invocable_r_v<void, F&, T&>);
    auto object = ref();
    object.runtime()->ops.onRetire(object.id(),
        [callback = std::forward<F>(callback)](void* ptr) mutable {
            std::invoke(callback, *static_cast<T*>(ptr));
        });
}

template<class T>
template<class F>
void ObjectBuild<T>::readyToDestroy(F&& callback) {
    static_assert(std::is_invocable_r_v<bool, F&, const T&>);
    auto object = ref();
    object.runtime()->ops.readyToDestroy(object.id(),
        [callback = std::forward<F>(callback)](const void* ptr) mutable {
            return static_cast<bool>(std::invoke(callback, *static_cast<const T*>(ptr)));
        });
}

template<class T>
template<class Run, class Stop>
void ObjectBuild<T>::service(Run run, Stop stop) {
    static_assert(std::is_invocable_v<Run&, T&> ||
        std::is_invocable_v<Run&, T&, const ServiceRunner::Idle&>,
        "Service run must accept (T&) or (T&, const std::function<void()>& idle)");
    static_assert(std::is_invocable_v<Stop&, T&>, "Service stop must accept (T&)");
    auto object = ref();
    std::function<void(const ServiceRunner::Idle&)> runService;
    if constexpr (std::is_invocable_v<Run&, T&, const ServiceRunner::Idle&>)
        runService = [object, run](const ServiceRunner::Idle& idle) mutable {
            std::invoke(run, *object, idle);
        };
    else
        runService = [object, run](const ServiceRunner::Idle&) mutable {
            std::invoke(run, *object);
        };
    std::function<void()> stopService = [object, stop]() mutable { std::invoke(stop, *object); };

    auto* runtime = object.runtime();
    if (runtime->ops.service(object.id()))
        throw Exception<ServiceRunner>("Object #{} already declares a service", object.id());
    runtime->ops.bindService(object.id(),
        std::make_unique<ServiceRunner>(std::move(runService), std::move(stopService)));
}

}
