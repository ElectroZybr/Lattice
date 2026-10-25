#pragma once

#include <mutex>
#include <utility>
#include <vector>

namespace Lattice {

/// A unit of work run by a pipeline: a stage, or a pipeline itself, so pipelines nest.
struct PipelineAPI {
    virtual ~PipelineAPI() = default;
    virtual void run() = 0;
};

/// Runs its stages in order. Composition assigns them from the tree (configurePipelines):
/// the children of the pipeline's node, in their order. The set may be replaced while
/// another thread runs the pipeline; the setter waits for the run to finish.
class Pipeline final : public PipelineAPI {
public:
    void setStages(std::vector<PipelineAPI*> stages) {
        std::lock_guard lock(mutex_);
        stages_ = std::move(stages);
    }

    void run() override {
        std::lock_guard lock(mutex_);
        for (PipelineAPI* stage : stages_)
            stage->run();
    }

    size_t size() const {
        std::lock_guard lock(mutex_);
        return stages_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<PipelineAPI*> stages_;
};

/// A method of an ordinary object as a stage: the object knows nothing of pipelines.
template<class T>
class CallStage final : public PipelineAPI {
public:
    CallStage(T& target, void (T::*method)()) : target_(target), method_(method) {}

    void run() override { (target_.*method_)(); }

private:
    T& target_;
    void (T::*method_)();
};

}
