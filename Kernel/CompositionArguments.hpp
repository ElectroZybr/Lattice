#pragma once

#include <type_traits>
#include <utility>
#include <vector>
#include <Lattice/Kernel/NodeHandlers.hpp>
#include <Lattice/Kernel/NodeSystem.hpp>

namespace Lattice::detail {

// Normalize aliases before taking a snapshot so slot replacement/id reuse expires old refs.
template<class T>
Ref<T> reference(NodeSystem& runtime, NodeId id) {
    const auto object = runtime.query.objectNode(id);
    const auto type = runtime.blueprints.find<T>();
    return Ref<T>{object == InvalidNodeId || type == InvalidBlueprintId ? nullptr :
        static_cast<T*>(runtime.query.resolve(object, type)), &runtime, object,
        object == InvalidNodeId ? 0 : runtime.registry.generation(object)};
}

template<class T>
T& resolveHandle(Ref<T> ref, NodeSystem& runtime) {
    if (!ref || ref.runtime() != &runtime || ref.id() == InvalidNodeId)
        throw Exception<NodeSystem>("Composition argument requires a live handle from this runtime");
    if (!runtime.registry.get(ref.id()) ||
        runtime.registry.generation(ref.id()) != ref.generation())
        throw Exception<NodeSystem>("Composition dependency #{} has expired", ref.id());
    const auto type = runtime.blueprints.id<T>();
    auto* current = static_cast<T*>(runtime.query.resolve(ref.id(), type));
    if (!current || current != ref.get())
        throw Exception<NodeSystem>("Composition dependency #{} is no longer available", ref.id());
    return *current;
}

template<class T>
T& attach(Ref<T> ref, NodeSystem& runtime, NodeId consumer) {
    auto& object = resolveHandle(ref, runtime);
    runtime.dependencies.retain(consumer, ref.id());
    return object;
}

template<class T>
struct CompositionArgument {
    template<class U>
    static decltype(auto) resolve(U&& value, NodeSystem&, NodeId) {
        return std::forward<U>(value);
    }
};

template<class T>
struct CompositionArgument<Ref<T>> {
    static T& resolve(Ref<T> value, NodeSystem& runtime, NodeId consumer) {
        return attach(value, runtime, consumer);
    }
};

template<class T>
struct CompositionArgument<ObjectBuild<T>> {
    static T& resolve(const ObjectBuild<T>& value, NodeSystem& runtime, NodeId consumer) {
        return attach(value.ref(), runtime, consumer);
    }
};

template<class T>
struct CompositionArgument<OptionalRef<T>> {
    static T* resolve(OptionalRef<T> value, NodeSystem& runtime, NodeId consumer) {
        if (value.runtime() && value.runtime() != &runtime)
            throw Exception<NodeSystem>("Optional dependency belongs to another runtime");
        return value ? &attach<T>(value, runtime, consumer) : nullptr;
    }
};

template<class T>
struct CompositionArgument<Selection<T>> {
    static std::vector<T*> resolve(const Selection<T>& values, NodeSystem& runtime, NodeId consumer) {
        std::vector<T*> result;
        result.reserve(values.size());
        for (auto value : values)
            result.push_back(&attach(value, runtime, consumer));
        return result;
    }
};

template<class T>
decltype(auto) compositionArgument(T&& value, NodeSystem& runtime, NodeId consumer) {
    return CompositionArgument<std::remove_cvref_t<T>>::resolve(std::forward<T>(value), runtime, consumer);
}

}
