#pragma once

#include <initializer_list>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <Lattice/kernel/Consts.hpp>
#include <Lattice/kernel/Visibility.hpp>
#include <Lattice/tools/TypeName.hpp>
#include <Lattice/tools/ObjectRegistry.hpp>

class NodeBuild;
class NodeConfigure;

namespace Lattice {

struct BlueprintOps {
    void (*destroy)(void*) = nullptr;
    void (*deallocate)(void*) = nullptr;
};

struct BlueprintMeta {
    void (*compose)(::NodeBuild) = nullptr;
    /// Repeatable wiring of the composed branch with the current tree (optional).
    void (*configure)(::NodeConfigure) = nullptr;
    // Slot composition owns a subtree and returns the node exposing its API.
    std::function<NodeId(::NodeBuild&)> composeSlot;
    void* (*allocate)() = nullptr;
    const BlueprintOps* ops = nullptr;
};

struct LATTICE_API Blueprint {
    std::string name;
    std::vector<BlueprintId> bases;
    BlueprintMeta meta;
    std::vector<void* (*)(void*)> upcasts;

    std::string_view shortName() const noexcept;
    std::string_view namespaceName() const noexcept;
};

class LATTICE_API Blueprints : public ObjectRegistry<Blueprint, BlueprintId, std::string> {
    using Base = ObjectRegistry<Blueprint, BlueprintId, std::string>;

public:
    BlueprintId addComposition(std::string_view name, void (*compose)(::NodeBuild),
        void (*configure)(::NodeConfigure) = nullptr);

    BlueprintId add(
        std::string_view name,
        std::initializer_list<BlueprintId> bases = {}
    );

    BlueprintId resolveImplementation(
        BlueprintId api,
        std::string_view nameSpace = {}
    ) const;

    BlueprintId find(
        std::string_view name,
        std::string_view nameSpace = {}
    ) const;

    void* cast(BlueprintId from, BlueprintId to, void* object) const;

    BlueprintId resolve(std::string_view name) const;

    void addBase(BlueprintId derived, BlueprintId base);

    bool isA(BlueprintId derived, BlueprintId base) const;

    void dumpTree() const;

    template<typename T>
    BlueprintId find() const {
        return find(typeKey<T>());
    }

    template<typename T>
    BlueprintId id() const {
        const BlueprintId result = find<T>();
        if (result == InvalidId)
            throw Exception<Blueprints>("Blueprint '{}' not found", typeKey<T>());
        return result;
    }

private:
    static void validateName(std::string_view name);
};

}
