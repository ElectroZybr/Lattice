#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <concepts>
#include <Lattice/kernel/Exports.hpp>
#include <string_view>
#include <vector>
#include <type_traits>
#include <utility>
#include <Lattice/tools/Exception.hpp>
#include <Lattice/tools/TypeName.hpp>
#include <Lattice/kernel/Blueprints.hpp>
#include <Lattice/kernel/Consts.hpp>
#include <Lattice/kernel/NodeContext.hpp>
#include <Lattice/kernel/NodeFactory.hpp>
#include <Lattice/kernel/NodeQuery.hpp>

namespace Lattice {

class NodeSystem;

template<typename T>
struct Ref {
    T* ptr = nullptr;

    Ref() = default;
    Ref(T* object, NodeSystem* runtime = nullptr, NodeId node = InvalidNodeId, uint64_t generation = 0)
        : ptr(object), runtime_(runtime), node_(node), generation_(generation) {}

    NodeId id() const noexcept { return node_; }
    NodeSystem* runtime() const noexcept { return runtime_; }
    uint64_t generation() const noexcept { return generation_; }

    T* get() const noexcept { return ptr; }
    T* operator->() const noexcept { return ptr; }
    T& operator*() const noexcept { return *ptr; }

    bool exists() const noexcept { return ptr != nullptr; }
    explicit operator bool() const noexcept { return ptr != nullptr; }

private:
    NodeSystem* runtime_ = nullptr;
    NodeId node_ = InvalidNodeId;
    uint64_t generation_ = 0;
};

// Build-only object capability. Runtime code receives ref(), never this builder.
template<class T>
class ObjectBuild : public Ref<T> {
public:
    explicit ObjectBuild(Ref<T> ref) : Ref<T>(ref) {}
    ObjectBuild(const ObjectBuild&) = delete;
    ObjectBuild& operator=(const ObjectBuild&) = delete;
    ObjectBuild(ObjectBuild&& other) noexcept
        : Ref<T>(std::exchange(static_cast<Ref<T>&>(other), Ref<T>{})) {}
    ObjectBuild& operator=(ObjectBuild&&) = delete;

    Ref<T> ref() const;
    template<class Value, class Owner> ExportId param(std::string_view name, Value Owner::* member);
    /// Export a value that lives inside this object (nested field, container element).
    template<class Value> ExportId param(std::string_view name, Value& value);
    /// Run this object's exports (params, actions, tables) on the thread of `queue`:
    /// `queue->call(work)` runs work there and returns when it is done. Declare it
    /// before the exports; the object then depends on the queue's node.
    template<class Queue> void executeOn(const ObjectBuild<Queue>& queue);
    // Methods may take ActionContext& as their first parameter.
    template<class Owner, class Result, class... Args>
    ExportId action(std::string_view name, Result (Owner::*member)(Args...));
    template<class Owner, class Result, class... Args>
    ExportId action(std::string_view name, Result (Owner::*member)(Args...) const);
    /// Action implemented in composition: callback(T&, [ActionContext&,] Args...).
    template<class... Args, class F>
        requires (!std::is_member_function_pointer_v<std::decay_t<F>>)
    ExportId action(std::string_view name, F&& callback);
    template<class Owner, class Result, class... Args>
    ExportId globalAction(std::string_view name, Result (Owner::*member)(Args...));
    template<class Owner, class Result, class... Args>
    ExportId globalAction(std::string_view name, Result (Owner::*member)(Args...) const);
    template<class F> void onRetire(F&& callback);
    /// Called on the host at each maintain(), only while this object is active.
    template<class F> void onMaintain(F&& callback);
    template<class F> void readyToDestroy(F&& callback);
    /// Run this object as the branch service: run(T&[, idle]) until stop(T&).
    template<class Run, class Stop> void service(Run run, Stop stop);
};

// Optional and collection arguments are snapshots of existing objects.
template<typename T>
struct OptionalRef : Ref<T> {
    using Ref<T>::Ref;
};

template<typename T>
class Selection {
public:
    Selection() = default;
    explicit Selection(std::vector<Ref<T>> values) : items_(std::move(values)) {}
    std::size_t size() const noexcept { return items_.size(); }
    bool empty() const noexcept { return items_.empty(); }
    const Ref<T>& operator[](std::size_t index) const { return items_[index]; }
    auto begin() const noexcept { return items_.begin(); }
    auto end() const noexcept { return items_.end(); }

private:
    std::vector<Ref<T>> items_;
};


/**
 * Selects an implementation, unlike Focus which selects an existing instance.
 * A composed implementation returns its API object from its owned subtree.
 * choice() returns false while old contents retire; the caller retries explicitly.
 */
template<typename T>
class Slot {
    NodeSystem* runtime_ = nullptr;
    NodeId id_ = InvalidNodeId;
    uint64_t generation_ = 0;
public:
    Slot() = default;
    Slot(NodeFactory& factory, NodeQuery&, Blueprints&, NodeId id);
    bool valid() const noexcept;
    T* get() const;
    T* operator->() const { return get(); }
    T& operator*() const { return *get(); }
    NodeId id() const noexcept { return id_; }
    bool exists() const { return get() != nullptr; }
    explicit operator bool() const { return exists(); }
    /// Snapshot of the selected object, suitable for constructor injection.
    Ref<T> ref() const;
    bool choice(BlueprintId implementation);
    /// Register a named composition inferred from an ordinary function's return handle.
    /// Reusing a name with another function/API is an error.
    template<class Result> bool choice(std::string_view name, Result (*compose)(::NodeBuild&),
        void (*configure)(::NodeConfigure) = nullptr);
};


/**
 * @brief Runtime handle to a contextual choice in one branch scope.
 *
 * Copies observe later branch focus/reset operations. ref() takes a snapshot for
 * constructor injection; changing focus never rebinds an existing C++ reference.
 * The handle neither owns the target nor registers a dependency by itself.
 */
template<typename T>
class Focus {
    NodeSystem* runtime_ = nullptr;
    ContextScopeId scope_ = InvalidContextScopeId;
    RoleId role_ = InvalidRoleId;
    NodeId owner_ = InvalidNodeId;
    uint64_t ownerGeneration_ = 0;

public:
    Focus() = default;
    Focus(NodeSystem& runtime, ContextScopeId scope, RoleId role);

    bool valid() const noexcept;
    NodeId id() const;
    T* get() const;
    T* operator->() const { return get(); }
    T& operator*() const { return *get(); }
    explicit operator bool() const { return get() != nullptr; }

    /// Resolve the current selection into a non-owning, generation-checked snapshot.
    /// Throws for a missing/ambiguous role or an expired scope.
    Ref<T> ref() const;

    /// Select a live compatible object as this scope's explicit target (runtime switch,
    /// e.g. the active controller under the cursor). Existing references are not rebound.
    template<class U> void set(const Ref<U>& target);
    /// Select by object: the node of `object` among the objects this scope may focus
    /// (its own subtree and visible candidates). Error when it is none of them.
    void set(T* object);

    /// Lazily (in maintain) call callback(owner&, focus) after this role changes, while
    /// `owner` is alive. The way to react to a Context change without polling.
    template<class U, class F> void onChange(const Ref<U>& owner, F callback) const;

    /// Remove this scope's explicit target and restore normal inherited resolution.
    void reset();
};

}

/// ссылка на объект дерева
using Lattice::Ref;

/// ссылка на слот апи в котором может находится его реализация
using Lattice::Slot;

/// контекстный выбор существующего объекта в scope ветки
using Lattice::Focus;
