#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include <lbm-runner/Benchmarks.hpp>
#include <lattice-test/Tests.hpp>

#include "Work_tests.hpp"

namespace Lattice {

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr std::chrono::nanoseconds GpuFixed = 20us, GpuPer = 10ns;

void spinFor(Clock::duration duration) {
    const auto until = Clock::now() + duration;
    while (Clock::now() < until) {}
}

/// An enum input axis: values in text through text/parse.
enum class Density { Low, High };

struct DensityAxis {
    using Value = Density;
    static constexpr std::string_view name = "density";
    static std::vector<Density> values() { return {Density::Low, Density::High}; }
    static std::string text(Density value) { return value == Density::Low ? "low" : "high"; }
    static Density parse(std::string_view text) { return text == "low" ? Density::Low : Density::High; }
};

/// What the input axis changed: the fixture sees it through the input.
struct DenseState {
    size_t n = 0;
    Density density = Density::Low;
    size_t done = 0;
};

struct DenseWork : Fixture {
    using Input = DenseState;
    DenseState state;
    explicit DenseWork(const DenseState& input) : state(input) {}
};

std::string message(auto&& function) {
    try {
        function();
    } catch (const ExceptionBase& error) {
        return error.what();
    }
    return {};
}

}

// The second implementation of TraceCompare/Spin (the work and cpu are in Trace_tests.cpp).
BENCH_IMPLEMENTATION(TraceCompare, Spin, "gpu", LinearWork) {
    impl.output([](LinearWork& work) { return work.state; });
    impl.measure([](LinearWork& work, Benchmarks::Run& run) {
        auto execute = run.trace().span(Benchmarks::Phase::Execute);
        spinFor(GpuFixed + GpuPer * work.state.n);
        work.state.done = work.state.n;
    });
}

BENCH_GROUP(WorkTests) {
    bench.config.sizes = {10, 100};
    bench.config.target = std::chrono::microseconds{0};
    bench.analysis.clear();
    bench.stages.clear();
    bench.stages.add<Benchmarks::Time>().samples(1);
}

BENCH_WORK(WorkTests, Dense, DenseState) {
    work.axis<DensityAxis>();
    work.input([](size_t n, const Benchmarks::Run& run) { return DenseState{n, run.value<DensityAxis>(), 0}; });
}

BENCH_IMPLEMENTATION(WorkTests, Dense, "a", DenseWork) {
    impl.measure([](DenseWork& work) { work.state.done = work.state.n * (work.state.density == Density::High ? 2 : 1); });
}

BENCH_IMPLEMENTATION(WorkTests, Dense, "b", DenseWork) {
    impl.measure([](DenseWork& work) { return work.state.n; });
}

BENCH_WORK(WorkTests, Wrong, SpinState) {
    work.input([](size_t n) { return SpinState{n, 0}; });
    work.check([](const SpinState& expected, const SpinState& actual) { return expected.done == actual.done; });
    work.reference("ref");
}

BENCH_IMPLEMENTATION(WorkTests, Wrong, "ref", LinearWork) {
    impl.output([](LinearWork& work) { return work.state; });
    impl.measure([](LinearWork& work) { work.state.done = work.state.n; });
}

BENCH_IMPLEMENTATION(WorkTests, Wrong, "bad", LinearWork) {
    impl.output([](LinearWork& work) { return work.state; });
    impl.measure([](LinearWork& work) { work.state.done = work.state.n - 1; });
}

BENCH_WORK(WorkTests, Twice, SpinState) {
    work.input([](size_t n) { return SpinState{n, 0}; });
}

BENCH_IMPLEMENTATION(WorkTests, Twice, "a", LinearWork) {
    impl.measure([](LinearWork&) {});
}

BENCH_IMPLEMENTATION(WorkTests, Twice, "a", LinearWork) {
    impl.measure([](LinearWork&) {});
}

BENCH_IMPLEMENTATION(WorkOrphans, Missing, "a", LinearWork) {
    impl.measure([](LinearWork&) {});
}

TEST(Work_InputAxisSplitsTheWork, Fixture,
    "Ось входа должна давать отдельные таблицы работы: каждая реализация на каждом значении, сравнение внутри значения.") {
    std::vector<Benchmarks::BenchResult> results;
    std::vector<Benchmarks::Comparison> comparisons;
    Benchmarks::disableCallbacks();
    Benchmarks::setCompleteCallback([&](const Benchmarks::BenchResult& result) { results.push_back(result); });
    Benchmarks::setComparisonCallback([&](const Benchmarks::Comparison& comparison) { comparisons.push_back(comparison); });

    const auto table = Benchmarks::runFiltered("WorkTests/Dense");
    REQUIRE(results.size() == 4);
    REQUIRE(results[0].implementation == "a" && results[0].inputs.size() == 1 && results[0].inputs[0].text == "low");
    REQUIRE(results[1].implementation == "a" && results[1].inputs[0].text == "high" && results[1].params.empty());
    REQUIRE(results[1].points.front().name == "Dense[density=high]/a");
    REQUIRE(comparisons.size() == 2 && comparisons[0].inputs == "[density=low]" && comparisons[0].executors.size() == 2);

    bool split = false;
    for (const auto& row : table.rows)
        if (row.bench == "Dense[density=high]" && row.variant == "b" && row.params == "-" && row.unit == "ns")
            split = true;
    REQUIRE(split);
    Benchmarks::resetCallbacks();
}

TEST(Work_ChecksAgainstTheReference, Fixture,
    "Реализация с другим результатом должна останавливать бенч до замеров с именем эталона.") {
    Benchmarks::disableCallbacks();
    const std::string error = message([] { Benchmarks::runFiltered("WorkTests/Wrong"); });
    REQUIRE(error.find("'WorkTests/Wrong/bad' differs from the reference 'ref' at N = 10") != std::string::npos);
    Benchmarks::resetCallbacks();
}

TEST(Work_RejectsBrokenRegistrations, Fixture,
    "Дубль реализации и реализация без работы должны быть ошибками с местом регистрации.") {
    Benchmarks::disableCallbacks();
    const std::string twice = message([] { Benchmarks::runFiltered("WorkTests/Twice"); });
    REQUIRE(twice.find("two implementations 'a'") != std::string::npos && twice.find("Work_tests.cpp:") != std::string::npos);
    const std::string orphan = message([] { Benchmarks::runFiltered("WorkOrphans/*"); });
    REQUIRE(orphan.find("'WorkOrphans/Missing/a'") != std::string::npos && orphan.find("has no work") != std::string::npos);
    Benchmarks::resetCallbacks();
}

TEST(Work_AxisValues, Fixture,
    "Значения оси: подписи, текст перечислений, разбор и сочетания осей.") {
    const auto workers = Benchmarks::Axes::values<TestWorkers>();
    REQUIRE(workers.size() == 2 && workers[0].label.empty() && workers[1].label == "×2" && workers[1].text == "2");
    const auto density = Benchmarks::Axes::values<DensityAxis>();
    REQUIRE(density[1].text == "high" && density[1].label == "[density=high]" && density[1].value == 1);
    REQUIRE(Benchmarks::Axes::parse<DensityAxis>("low") == Density::Low);
    REQUIRE(Benchmarks::Axes::parse<TestWorkers>("16") == 16);

    const auto all = Benchmarks::combinations({Benchmarks::axisDecl<DensityAxis>(), Benchmarks::axisDecl<TestWorkers>()});
    REQUIRE(all.size() == 4 && all[1][0].text == "low" && all[1][1].text == "2" && all[2][0].text == "high");
    REQUIRE(Benchmarks::combinations({}).size() == 1);

    const Benchmarks::Run run(nullptr, density);
    REQUIRE(run.value<DensityAxis>() == Density::Low);
    REQUIRE(!message([&] { (void)run.value<TestWorkers>(); }).empty());
}

}
