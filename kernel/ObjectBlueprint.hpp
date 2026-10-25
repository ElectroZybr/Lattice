#pragma once

#include <algorithm>
#include <memory>
#include <type_traits>
#include <Lattice/kernel/Blueprints.hpp>

namespace Lattice::detail {

// Plain types have no implicit configure/retire/descriptor conventions.
template<typename T, typename... Bases>
BlueprintId ensureObject(Blueprints& blueprints, std::string_view name = typeKey<T>()) {
    static_assert((std::is_convertible_v<T*, Bases*> && ...),
        "Provided APIs must be public unambiguous C++ bases");
    // Register exposed interfaces before taking any references into the registry.
    (ensureObject<Bases>(blueprints), ...);
    const auto expose = [&](BlueprintId id) {
        const auto addBase = [&]<class Base>() {
            const auto api = blueprints.id<Base>();
            auto& blueprint = blueprints.require(id);
            if (std::ranges::find(blueprint.bases, api) == blueprint.bases.end()) {
                blueprint.bases.push_back(api);
                blueprint.upcasts.push_back(+[](void* ptr) -> void* {
                    return static_cast<Base*>(static_cast<T*>(ptr));
                });
            }
        };
        (addBase.template operator()<Bases>(), ...);
        return id;
    };
    static const BlueprintOps ops{
        .destroy = [](void* ptr) {
            auto* object = static_cast<T*>(ptr);
            std::destroy_at(object);
            std::allocator<T>{}.deallocate(object, 1);
        },
        .deallocate = [](void* ptr) { std::allocator<T>{}.deallocate(static_cast<T*>(ptr), 1); }
    };
    const auto allocate = []() -> void* { return std::allocator<T>{}.allocate(1); };
    if (const auto existing = blueprints.find<T>(); existing != InvalidBlueprintId) {
        // A name registered before its type was (the API of an implementation) gets the storage now.
        auto& blueprint = blueprints.require(existing);
        if (!blueprint.meta.allocate) {
            blueprint.meta.allocate = allocate;
            blueprint.meta.ops = &ops;
        }
        return expose(existing);
    }
    Blueprint blueprint{
        .name = std::string(name),
        .bases = {blueprints.id<Bases>()...}
    };
    blueprint.upcasts = {+[](void* ptr) -> void* {
        return static_cast<Bases*>(static_cast<T*>(ptr));
    }...};
    blueprint.meta.allocate = allocate;
    blueprint.meta.ops = &ops;
    return expose(blueprints.create(std::move(blueprint), std::string(name)));
}

}
