#pragma once

#include <Lattice/kernel/NodeFactory.hpp>
#include <Lattice/kernel/Builder.hpp>
#include <Lattice/kernel/DependencyGraph.hpp>
#include <Lattice/kernel/NodeRegistry.hpp>
#include <Lattice/kernel/NodeQuery.hpp>
#include <Lattice/kernel/NodeContext.hpp>
#include <Lattice/kernel/NodeOps.hpp>
#include <Lattice/kernel/Exports.hpp>
#include <Lattice/kernel/Choices.hpp>
#include <Lattice/kernel/Visibility.hpp>

namespace Lattice {

class LT_API NodeSystem {
public:
    Blueprints& blueprints;
    NodeRegistry registry;
    DependencyGraph dependencies;
    NodeFactory factory;
    NodeContext context;
    NodeQuery query;
    Exports exports;
    Choices choices;
    NodeOps ops;
    Builder builder;

    NodeSystem(Blueprints& blueprints)
        : blueprints(blueprints)
        , factory(*this)
        , context(registry)
        , query(registry, blueprints, context)
        , choices(*this)
        , ops(*this)
        , builder(*this) {}

    NodeSystem(const NodeSystem&) = delete;
    NodeSystem& operator=(const NodeSystem&) = delete;
};

}
