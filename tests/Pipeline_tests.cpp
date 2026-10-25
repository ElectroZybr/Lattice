#include <string>
#include <vector>
#include <Lattice/Composition.hpp>
#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/tools/Tests.hpp>

namespace Lattice::PipelineTests {
namespace {

std::vector<std::string> journal;

struct Recorder {
    explicit Recorder(std::string name) : name(std::move(name)) {}
    void record() { journal.push_back(name); }
    std::string name;
};

NodeId stage(NodeBuild& branch, std::string_view name) {
    auto recorder = branch.emplace<Recorder>(std::string(name) + ".recorder", std::string(name));
    return branch.emplace<CallStage<Recorder>, PipelineAPI>(name, recorder, &Recorder::record).id();
}

// first → inner (second, third) → last; the branch object is the outer pipeline.
void composeStep(NodeBuild root) {
    stage(root, "first");
    auto inner = root.branch("inner");
    stage(inner, "second");
    stage(inner, "third");
    inner.construct<Pipeline, PipelineAPI>();
    stage(root, "last");
    root.construct<Pipeline, PipelineAPI>();
}

void configureStep(NodeConfigure root) {
    configurePipelines(root);
}

struct Step {
    NodeId branch;
    Pipeline* pipeline;
};

Step addStep(RuntimeFixture& fixture) {
    auto& blueprints = fixture.run_ctx.blueprints;
    auto type = blueprints.find("PipelineStep");
    if (type == InvalidBlueprintId)
        type = blueprints.addComposition("PipelineStep", composeStep, configureStep);
    const auto branch = fixture.run_ctx.nodes.builder.add(fixture.root, type, "step");
    return {branch, static_cast<Pipeline*>(fixture.run_ctx.nodes.registry.require(branch).object.ptr)};
}

std::vector<std::string> run(Pipeline& pipeline) {
    journal.clear();
    pipeline.run();
    return journal;
}

}

TEST(Pipeline_RunsChildrenInTreeOrder, RuntimeFixture,
    "Конвейер выполняет детей своего узла по порядку, вложенный конвейер — как одну стадию.")
{
    const auto step = addStep(fixture);
    REQUIRE(step.pipeline->size() == 3);
    REQUIRE((run(*step.pipeline) == std::vector<std::string>{"first", "second", "third", "last"}));
}

TEST(Pipeline_FollowsTheTree, RuntimeFixture,
    "Новая стадия встаёт в конец после maintain, retire стадии убирает её из конвейера.")
{
    auto& nodes = fixture.run_ctx.nodes;
    const auto step = addStep(fixture);
    auto branch = fixture.build(step.branch);
    const NodeId extra = stage(branch, "extra");
    nodes.ops.maintain();
    REQUIRE((run(*step.pipeline) == std::vector<std::string>{"first", "second", "third", "last", "extra"}));

    nodes.ops.retireBranch(extra);
    nodes.ops.maintain();
    REQUIRE((run(*step.pipeline) == std::vector<std::string>{"first", "second", "third", "last"}));
}

}
