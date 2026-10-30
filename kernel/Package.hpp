#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <Lattice/kernel/Visibility.hpp>

class NodeBuild;
class NodeConfigure;

namespace Lattice {

/**
 * An implementation of a slot API, declared by its package in `lattice_package`:
 *
 *     constinit const Lattice::Implementation implementations[] = {
 *         Lattice::implementation<Renderer, composeHardware>("Hardware"),
 *     };
 *     extern "C" constinit const Lattice::Package lattice_package{
 *         .id = "Render", .version = {1, 0, 0}, .implementations = implementations,
 *     };
 *
 * Every slot of that API sees it as a choice, named `Render::Hardware`.
 */
struct Implementation {
    std::string_view api;
    std::string_view name;
    /// Builds the implementation on the slot node and returns that node.
    uint32_t (*compose)(::NodeBuild&) = nullptr;
    void (*configure)(::NodeConfigure) = nullptr;
};

struct Version {
    uint8_t major = 0;
    uint8_t minor = 0;
    uint8_t patch = 0;

    auto operator<=>(const Version&) const = default;

    std::string str() const {
        return std::format("{}.{}.{}", major, minor, patch);
    }
};

/// Kernel API the headers describe. A package records the value it was compiled with.
inline constexpr Version kernelApi{1, 0, 0};

/// Requires package `id` with version in [min, before).
struct PackageDependency {
    std::string_view id;
    Version min{};
    std::optional<Version> before{};
};

inline constexpr size_t maxPackageDependencies = 16;

/**
 * Package metadata, declared in compose.cpp next to the composition:
 *
 *     extern "C" constinit const Lattice::Package lattice_package{
 *         .id = "Shell",
 *         .version = {1, 0, 0},
 *         .dependencies = {{.id = "Actions", .min = {1, 0, 0}}, {.id = "Graphics"}},
 *     };
 *
 * The loader opens the package library, reads this object and orders loading by
 * dependencies. `kernelApi` defaults to the SDK the package was built against.
 *
 * Plain constant data on purpose: every package exports the same symbol, and a
 * dynamically initialized object would be constructed through an interposed
 * symbol of an earlier loaded package. `constinit` keeps it static.
 */
struct Package {
    std::string_view id;
    std::string_view name{};
    Version version{};
    PackageDependency dependencies[maxPackageDependencies]{};
    Version kernelApi = Lattice::kernelApi;
    /// Slot implementations this package provides.
    std::span<const Implementation> implementations{};

    /// Declared dependencies (the array is terminated by the first empty id).
    std::span<const PackageDependency> dependencyList() const noexcept {
        size_t count = 0;
        while (count < maxPackageDependencies && !dependencies[count].id.empty())
            ++count;
        return {dependencies, count};
    }
};

inline constexpr std::string_view packageSymbol = "lattice_package";

}

// The entry points of a package, declared exported: its definitions take the export from here.
extern "C" {
LT_PACKAGE extern const Lattice::Package lattice_package;
LT_PACKAGE void compose(NodeBuild root);
LT_PACKAGE void configure(NodeConfigure root);
}
