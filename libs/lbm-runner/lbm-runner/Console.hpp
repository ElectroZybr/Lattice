#pragma once
#include <memory>
#include <string_view>
#include <lbm-runner/BenchTypes.hpp>

namespace Lattice::Benchmarks::Output {
enum class Mode { Progress, ProgressAndText, Text };

// Presentation for a CLI run. No output is performed inside measured iterations.
class Console {
public:
    Console(Mode mode, size_t total);
    ~Console();
    void start(std::string_view name);
    void sample(const SampleResult& sample);
    void advance();
    void finish();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
