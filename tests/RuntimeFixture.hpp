#pragma once

#include "Lattice/kernel/BasicTable.hpp"
#include "Lattice/kernel/Context.hpp"
#include "Lattice/kernel/DLLoader.hpp"
#include "Lattice/kernel/NodeViews.hpp"
#include "Lattice/kernel/PackageManager.hpp"
#include "Lattice/tools/Fixture.hpp"

namespace Lattice {

struct RuntimeFixture : public Fixture {
    Context run_ctx;
    NodeId root = InvalidNodeId;
    DLLoader dlLoader;
    PackageManager packageManager;

    ~RuntimeFixture() override { run_ctx.nodes.ops.destroyBranch(root); }

    RuntimeFixture() : packageManager(run_ctx.blueprints, dlLoader) {
        root = run_ctx.nodes.factory.folder(InvalidNodeId, "Root");
    }

    ::NodeBuild build(NodeId id) {
        run_ctx.nodes.registry.require(id);
        return ::NodeBuild{id, run_ctx.nodes};
    }
};

}
