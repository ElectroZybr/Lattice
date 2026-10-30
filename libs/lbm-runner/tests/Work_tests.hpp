#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <lattice-tools/Fixture.hpp>

namespace Lattice {

/// State of the synthetic work: the step "does" n elements.
struct SpinState {
    size_t n = 0;
    size_t done = 0;
};

/// Work with a known price: fixed + per·N.
struct LinearWork : Fixture {
    using Input = SpinState;
    SpinState state;
    explicit LinearWork(const SpinState& input) : state(input) {}
    void prepare() override { state.done = 0; }
};

/// An execution axis as a module of the mechanism would declare it: no BmRunner header.
struct TestWorkers {
    using Value = size_t;
    static constexpr std::string_view name = "workers";
    static std::vector<size_t> values() { return {1, 2}; }
    static std::vector<size_t> sweep() { return {1, 2, 4}; }
    static std::string label(size_t n) { return n == 1 ? "" : "×" + std::to_string(n); }
};

}
