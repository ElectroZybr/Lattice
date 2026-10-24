#pragma once

#include <Lattice/Kernel/NodeViews.hpp>
#include <Lattice/Kernel/PipelineAPI.hpp>

namespace Lattice {

/// Configure helper: every Pipeline of the branch (its own object included) runs the
/// PipelineAPI children of its node, in tree order.
inline void configurePipelines(const NodeConfigure& root) {
    if (Pipeline* self = root.self<Pipeline>())
        self->setStages(root.children<PipelineAPI>());
    for (auto [pipeline, node] : root.collectNodes<Pipeline>())
        pipeline->setStages(node.children<PipelineAPI>());
}

}
