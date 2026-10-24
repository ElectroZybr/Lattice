#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <BmRunner/Benchmarks.hpp>
#include "Work_tests.hpp"
#include <Lattice/Tools/Tests.hpp>

namespace Lattice {

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr auto PrepareTime = 20us;
constexpr auto ExecuteTime = 60us;
constexpr size_t TransferBytes = 1 << 20;

void spin(Clock::duration duration) {
    const auto until = Clock::now() + duration;
    while (Clock::now() < until) {}
}

/// Синтетический асинхронный исполнитель: поток, который по команде работает заданное
/// время и сам меряет свою работу (как timestamps устройства или время сервера).
struct AsyncExecutor : Fixture {
    std::atomic<int> jobs{0};
    std::atomic<int> done{0};
    std::atomic<bool> stop{false};
    std::atomic<double> executed{0};   // нс работы исполнителя с прошлого отчёта
    std::vector<char> source = std::vector<char>(TransferBytes, 1);
    std::vector<char> device = std::vector<char>(TransferBytes);
    std::thread worker;

    explicit AsyncExecutor(size_t)
        : worker([this] {
              int seen = 0;
              while (!stop.load(std::memory_order_acquire)) {
                  if (jobs.load(std::memory_order_acquire) == seen)
                      continue;
                  ++seen;
                  const auto started = Clock::now();
                  spin(ExecuteTime);
                  const double ns = std::chrono::duration<double, std::nano>(Clock::now() - started).count();
                  executed.store(executed.load(std::memory_order_relaxed) + ns, std::memory_order_relaxed);
                  done.store(seen, std::memory_order_release);
              }
          }) {}

    ~AsyncExecutor() override {
        stop.store(true, std::memory_order_release);
        worker.join();
    }
};

double metric(const Benchmarks::BenchResult& result, std::string_view capability, std::string_view name) {
    for (const auto& point : result.points)
        for (const auto& stage : point.stages)
            for (const auto& metrics : stage.capabilities) {
                if (metrics.capability != capability)
                    continue;
                for (size_t i = 0; i < metrics.metrics.schema.size(); ++i)
                    if (metrics.metrics.schema[i].name == name)
                        return metrics.metrics.values.at(i);
            }
    return -1.0;
}

bool hasCapability(const Benchmarks::BenchResult& result, std::string_view capability) {
    for (const auto& point : result.points)
        for (const auto& stage : point.stages)
            for (const auto& metrics : stage.capabilities)
                if (metrics.capability == capability)
                    return true;
    return false;
}

}

BENCH_GROUPED(TraceSynthetic, TraceAsyncExecutor, AsyncExecutor) {
    bench.config.sizes = {1};
    bench.config.target = std::chrono::microseconds{500};
    bench.analysis.clear();
    bench.stages.clear();
    bench.stages.add<Benchmarks::Time>().samples(5);

    bench.measure<AsyncExecutor>([](AsyncExecutor& executor, Benchmarks::Trace& trace) {
        using Benchmarks::Phase;
        {
            auto prepare = trace.span(Phase::Prepare);
            spin(PrepareTime);
        }
        {
            auto transfer = trace.span(Phase::TransferIn, "data");
            std::memcpy(executor.device.data(), executor.source.data(), TransferBytes);
            transfer.bytes(TransferBytes);
        }
        const int job = executor.jobs.load(std::memory_order_relaxed) + 1;
        {
            auto issue = trace.span(Phase::Issue);
            executor.jobs.store(job, std::memory_order_release);
        }
        {
            auto complete = trace.span(Phase::Complete);
            while (executor.done.load(std::memory_order_acquire) < job) {}
        }
        // Время исполнителя известно после итерации: забрать в конце семпла.
        trace.defer([&executor](Benchmarks::Trace& report) {
            report.duration(Phase::Execute, executor.executed.exchange(0.0));
        });
    });
}

BENCH_GROUPED(TraceSynthetic, TraceUntraced, AsyncExecutor) {
    bench.config.sizes = {1};
    bench.config.target = std::chrono::microseconds{0};
    bench.analysis.clear();
    bench.stages.clear();
    bench.stages.add<Benchmarks::Time>().samples(1);

    bench.measure<AsyncExecutor>([](AsyncExecutor& executor) { return executor.jobs.load(); });
}

constexpr std::chrono::nanoseconds CpuFixed = 1us, CpuPer = 100ns;

BENCH_GROUP(TraceCompare) {
    bench.config.sizes = {10, 100, 1000};
    bench.config.target = std::chrono::microseconds{200};
    bench.analysis.clear();
    bench.stages.clear();
    bench.stages.add<Benchmarks::Time>().samples(5);
}

// One work, three variants: cpu with one and two workers (here), gpu (Work_tests.cpp — another
// translation unit, as an implementation from another module would be).
BENCH_WORK(TraceCompare, Spin, SpinState, "work with a known price: fixed + per·N") {
    work.input([](size_t n) { return SpinState{n, 0}; });
    work.check([](const SpinState& expected, const SpinState& actual) { return expected.done == actual.done; });
    work.reference("cpu");
}

BENCH_IMPLEMENTATION(TraceCompare, Spin, "cpu", LinearWork) {
    impl.axis<TestWorkers>();
    impl.output([](LinearWork& work) { return work.state; });
    impl.measure([](LinearWork& work, Benchmarks::Run& run) {
        auto execute = run.trace().span(Benchmarks::Phase::Execute);
        const double workers = double(run.value<TestWorkers>());
        spin(CpuFixed + std::chrono::nanoseconds(int64_t(double(CpuPer.count()) * double(work.state.n) / workers)));
        work.state.done = work.state.n;
    });
}

TEST(Trace_ComparesImplementations, Fixture,
    "Реализации одной работы с осями должны сравниваться: подгонка fixed + per·N и пороги N*.") {
    std::vector<Benchmarks::Comparison> comparisons;
    std::vector<Benchmarks::BenchResult> results;
    Benchmarks::disableSampleCallback();
    Benchmarks::disableResultCallback();
    Benchmarks::setCompleteCallback([&](const Benchmarks::BenchResult& result) { results.push_back(result); });
    Benchmarks::setComparisonCallback([&](const Benchmarks::Comparison& comparison) { comparisons.push_back(comparison); });

    Benchmarks::runGroup("TraceCompare");
    REQUIRE(results.size() == 3);
    REQUIRE(results[0].name == "Spin" && results[0].implementation == "cpu" && results[0].label() == "cpu");
    REQUIRE(results[1].label() == "cpu×2" && results[1].params.size() == 1 && results[1].params[0].value == 2);
    REQUIRE(results[1].params[0].text == "2" && results[1].variant == "cpu×2");
    REQUIRE(results[2].label() == "gpu" && results[2].params.empty());
    REQUIRE(results[1].points.front().name == "Spin/cpu×2");

    // Фаза Execute подогнана прямой: задержка и цена на элемент.
    const Benchmarks::AnalysisResult* execute = nullptr;
    for (const auto& analysis : results[0].analysis)
        if (analysis.type == Benchmarks::AnalysisType::Affine && analysis.y.name == "Execute")
            execute = &analysis;
    REQUIRE(execute != nullptr);
    const double fixed = std::get<double>(execute->find("fixed")->value);
    const double per = std::get<double>(execute->find("per")->value);
    REQUIRE(std::abs(fixed - 1000.0) < 300.0 && std::abs(per - 100.0) < 10.0);

    REQUIRE(comparisons.size() == 1);
    const auto& comparison = comparisons[0];
    REQUIRE(comparison.work == "Spin" && comparison.executors.size() == 3 && comparison.crossovers.size() == 3);
    auto threshold = [&](std::string_view faster, std::string_view slower) {
        for (const auto& crossover : comparison.crossovers)
            if (crossover.faster == faster && crossover.slower == slower)
                return crossover.n;
        return -1.0;
    };
    // N* = (20 мкс − 1 мкс) / (100 нс − 10 нс) ≈ 211; с двумя потоками — / (50 − 10) ≈ 475;
    // у двух потоков та же задержка и меньше цена на элемент — быстрее везде.
    REQUIRE(std::abs(threshold("gpu", "cpu") - 211.0) < 211.0 * 0.15);
    REQUIRE(std::abs(threshold("gpu", "cpu×2") - 475.0) < 475.0 * 0.15);
    REQUIRE(threshold("cpu×2", "cpu") >= 0 && threshold("cpu×2", "cpu") < 50);

    Benchmarks::disableCallbacks();
}

TEST(Trace_AffineFitIsNonNegative, Fixture,
    "Подгонка fixed + per·x не должна давать отрицательных частей на шумных данных.") {
    // Чистая пропорциональность с шумом: МНК дали бы fixed < 0.
    const std::vector<Benchmarks::ComplexityPoint> points{{1000, 40000}, {10000, 460000}, {100000, 4600000}};
    const auto fit = Benchmarks::fitAffine(points);
    REQUIRE(fit.valid && fit.fixed == 0.0 && fit.per > 40.0 && fit.per < 47.0);

    const auto exact = Benchmarks::fitAffine(std::vector<Benchmarks::ComplexityPoint>{{10, 1100}, {100, 2000}, {1000, 11000}});
    REQUIRE(std::abs(exact.fixed - 1000.0) < 1e-6 && std::abs(exact.per - 10.0) < 1e-9 && exact.error < 1e-9);
}

TEST(Trace_DecomposesAsyncWork, Fixture,
    "Trace должен раскладывать асинхронную работу на фазы хоста, время исполнителя и остаток.") {
    Benchmarks::BenchResult completed;
    Benchmarks::disableSampleCallback();
    Benchmarks::disableResultCallback();
    Benchmarks::setCompleteCallback([&](const Benchmarks::BenchResult& result) { completed = result; });

    Benchmarks::run("TraceAsyncExecutor");
    const double prepare = metric(completed, "Phases", "Prepare");
    const double transfer = metric(completed, "Phases", "Transfer.In:data");
    const double rate = metric(completed, "Phases", "Transfer.In:data.rate");
    const double complete = metric(completed, "Phases", "Complete");
    const double execute = metric(completed, "Phases", "Execute@dev");
    const double wall = metric(completed, "Phases", "wall");
    const double unaccounted = metric(completed, "Phases", "Unaccounted");

    const double prepareNs = std::chrono::duration<double, std::nano>(PrepareTime).count();
    const double executeNs = std::chrono::duration<double, std::nano>(ExecuteTime).count();
    // Метки на итерацию: фаза не короче заданной и не намного длиннее.
    REQUIRE(prepare >= prepareNs && prepare < prepareNs * 2);
    REQUIRE(execute >= executeNs && execute < executeNs * 2);
    // Ожидание хоста включает работу исполнителя.
    REQUIRE(complete >= execute * 0.9);
    REQUIRE(transfer > 0 && rate > 0);
    REQUIRE(std::abs(rate - double(TransferBytes) * 1e9 / transfer) < rate * 1e-6);
    // Фазы хоста покрывают почти всю стену; исполнитель из стены не вычитается.
    REQUIRE(wall >= prepare + transfer + complete);
    REQUIRE(unaccounted >= 0 && unaccounted < wall * 0.1);

    Benchmarks::run("TraceUntraced");
    REQUIRE(!hasCapability(completed, "Phases"));

    Benchmarks::disableCallbacks();
}

TEST(Trace_KeepsPhasesAcrossSamples, Fixture,
    "Trace должен сохранять набор фаз между семплами и обнулять значения.") {
    Benchmarks::Trace trace;
    {
        auto span = trace.span(Benchmarks::Phase::Prepare, "a");
        span.bytes(10);
    }
    trace.duration(Benchmarks::Phase::Execute, 5.0);
    trace.defer([](Benchmarks::Trace& report) { report.duration(Benchmarks::Phase::Execute, 7.0); });
    trace.flush();
    REQUIRE(trace.entries().size() == 2);
    REQUIRE(trace.entries()[0].bytes == 10 && trace.entries()[0].count == 1 && trace.spans() == 1);
    REQUIRE(trace.entries()[1].nanoseconds == 12.0 && trace.entries()[1].count == 2);

    trace.reset();
    REQUIRE(trace.entries().size() == 2 && trace.entries()[1].nanoseconds == 0 && trace.spans() == 0);
}

TEST(Trace_TakesIntervalsOfAnyTimer, Fixture,
    "Trace должен принимать интервалы любого таймера с read() и экстраполировать неизмеренные.") {
    struct Interval { std::string name; double nanoseconds; uint64_t count, skipped; };
    struct FakeTimer {
        int reads = 0;
        std::vector<Interval> read() {
            return reads++ ? std::vector<Interval>{} : std::vector<Interval>{{"kernel", 30.0, 3, 1}, {"idle", 0.0, 0, 2}};
        }
    } timer;

    Benchmarks::Trace trace;
    trace.deferIntervals(timer);
    trace.deferIntervals(timer);   // вторая итерация того же семпла
    trace.flush();
    REQUIRE(timer.reads == 2 && trace.entries().size() == 1);
    const auto& kernel = trace.entries()[0];
    REQUIRE(kernel.label == "kernel" && kernel.source == Benchmarks::TimeSource::Executor);
    REQUIRE(kernel.nanoseconds == 40.0 && kernel.phase == Benchmarks::Phase::Execute);
}

}
