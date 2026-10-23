#pragma once

#include "Lattice/Kernel/BasicTable.hpp"
#include "Lattice/Kernel/Context.hpp"
#include "Lattice/Kernel/DLLoader.hpp"
#include "Lattice/Kernel/NodeViews.hpp"
#include "Lattice/Kernel/PluginManager.hpp"
#include "Lattice/Tools/Fixture.hpp"

namespace Lattice {

struct RuntimeFixture : public Fixture {
    Context run_ctx;
    NodeId root = InvalidNodeId;
    DLLoader dlLoader;
    PluginManager pluginManager;

    ~RuntimeFixture() override { run_ctx.nodes.ops.destroyBranch(root); }

    RuntimeFixture() : pluginManager(run_ctx.blueprints, dlLoader) {
        root = run_ctx.nodes.factory.folder(InvalidNodeId, "Root");
    }

    ::NodeBuild build(NodeId id) {
        run_ctx.nodes.registry.require(id);
        return ::NodeBuild{id, run_ctx.nodes};
    }
};

}
