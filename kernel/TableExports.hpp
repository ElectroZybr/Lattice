#pragma once

#include <concepts>
#include <cstdint>

#include <lattice-tools/TableAPI.hpp>
#include <Lattice/kernel/NodeViews.hpp>

namespace Lattice {

/// Composition helper: export the common Table actions of a concrete object.
/// Table itself is a plain contract and does not register anything.
template<std::derived_from<Table> T>
void exportTable(const ObjectBuild<T>& table) {
    auto object = table.ref();
    ::NodeBuild node{object.id(), *object.runtime()};
    node.action("view", [object](ActionContext& context) {
        context.present(static_cast<const Table&>(*object));
    });
    node.action("addRow", [object](ActionContext& context) {
        context.emit(Value{static_cast<int64_t>(static_cast<Table&>(*object).addRow())});
    });
}

}
