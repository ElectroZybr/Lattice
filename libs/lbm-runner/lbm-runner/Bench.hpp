#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <algorithm>

#include <lbm-runner/Axis.hpp>
#include <lbm-runner/BenchTypes.hpp>
#include <lbm-runner/Visibility.hpp>
#include <lbm-runner/Analysis.hpp>
#include <lbm-runner/Stages.hpp>
#include <lbm-runner/Metrics.hpp>
#include <lbm-runner/Trace.hpp>
#include <deque>
#include <memory>
#include <thread>
#include <lattice-tools/Exception.hpp>
#include <lattice-tools/Fixture.hpp>


namespace Lattice::Benchmarks {

/**
 @file Bench.hpp
 @brief Выполнение и измерение отдельного бенчмарка.

 Bench управляет параметрами запуска, подготовкой входных данных,
 калибровкой числа итераций и выполнением стадий измерения.

 Промежуточные и итоговые результаты передаются через callback-и
 после семплов, отдельных значений N и полного завершения бенчмарка.
*/

class Bench {
    using Clock = std::chrono::steady_clock;

public:
    struct Config {
        std::vector<size_t> sizes;
        size_t samples = 9;
        std::chrono::microseconds target{50};
    };

private:
    std::string group_;
    std::string name_;

public:
    using FixtureFactory = std::function<std::unique_ptr<Fixture>(size_t)>;

    /// Вариант работы, который измеряется сейчас (см. Work.hpp).
    struct Variant {
        std::string implementation;
        std::vector<AxisValue> inputs;   // оси входа
        std::vector<AxisValue> params;   // оси исполнения
    };

private:
    FixtureFactory createFixture_;
    std::unique_ptr<Fixture> shared_;   // the point's fixture when it is reused
    Trace trace_;
    Run run_{&trace_, {}};

    Variant variant_;
    std::string label_;   // work[inputs]/variant: names of points and samples
    const SampleCallback& sample_;
    const ResultCallback& result_;
    const CompleteCallback& complete_;

public:
    Config config{};
    Stages stages;
    Analysis analysis{};

    Bench(
        std::string_view group,
        std::string_view name,
        FixtureFactory createFixture,
        const SampleCallback& sample,
        const ResultCallback& result,
        const CompleteCallback& complete
    )
        : group_(group)
        , name_(name)
        , createFixture_(createFixture)
        , sample_(sample)
        , result_(result)
        , complete_(complete) {

        stages.add<Warmup>().samples(16).time(std::chrono::milliseconds(10));
        stages.add<Time>().samples(10);
        analysis.growth(N, Time::median);
    }

    std::string_view group() const noexcept {
        return group_;
    }

    std::string_view name() const noexcept {
        return name_;
    }

    /// Измерить вариант работы: `createFixture(n)` строит фикстуру из входа работы.
    template<typename FixtureType, typename Function>
    void measureVariant(Variant variant, FixtureFactory createFixture, Function& function) {
        variant_ = std::move(variant);
        trace_.clear();
        std::vector<AxisValue> values = variant_.inputs;
        values.insert(values.end(), variant_.params.begin(), variant_.params.end());
        run_ = Run(&trace_, std::move(values));
        label_ = name_ + inputLabel(parameters(variant_.inputs)) + "/" + variantName();
        struct Clear {
            Bench& bench;
            ~Clear() { bench.variant_ = {}; bench.run_ = Run(&bench.trace_, {}); bench.label_.clear(); }
        } clear{*this};
        measureWith<FixtureType>(std::move(createFixture), function);
    }

    /// Имя измеряемого варианта: `cpu×16`; пусто вне вариантов.
    std::string variantName() const {
        return variant_.implementation.empty() ? std::string{} : variant_.implementation + labels(variant_.params);
    }

    template<typename FixtureType, typename Function>
    void measure(Function&& function) {
        measureWith<FixtureType>(createFixture_, function);
    }

    template<typename FixtureType, typename Function>
    void measureWith(FixtureFactory createFixture, Function& function) {
        FixtureFactory benchFactory = createFixture_;
        createFixture_ = std::move(createFixture);
        struct Restore {
            Bench& bench;
            FixtureFactory factory;
            ~Restore() { bench.createFixture_ = std::move(factory); }
        } restore{*this, std::move(benchFactory)};

        if (config.samples == 0)
            throw Exception<Bench>("Samples count cannot be zero");

        constexpr size_t operations = operationsPerIteration<FixtureType>();
        static_assert(operations > 0, "FixtureType::Steps must be greater than zero");

        std::vector<PointResult> points;
        points.reserve(config.sizes.size());

        // Функция бенча может принимать вторым аргументом Run (контекст варианта) или
        // Trace — разметку фаз.
        constexpr bool withRun = std::is_invocable_v<Function&, FixtureType&, Run&>;
        constexpr bool traced = withRun || std::is_invocable_v<Function&, FixtureType&, Trace&>;
        if constexpr (traced)
            addPhases();

        auto invoke = [&](Fixture& fixture) {
            auto& typed = static_cast<FixtureType&>(fixture);

            if constexpr (withRun) {
                using Result = std::invoke_result_t<Function&, FixtureType&, Run&>;
                if constexpr (std::is_void_v<Result>) {
                    std::invoke(function, typed, run_);
                } else {
                    auto value = std::invoke(function, typed, run_);
                    doNotOptimize(value);
                }
            } else if constexpr (traced) {
                using Result = std::invoke_result_t<Function&, FixtureType&, Trace&>;
                if constexpr (std::is_void_v<Result>) {
                    std::invoke(function, typed, trace_);
                } else {
                    auto value = std::invoke(function, typed, trace_);
                    doNotOptimize(value);
                }
            } else if constexpr (std::is_void_v<std::invoke_result_t<Function&, FixtureType&>>) {
                std::invoke(function, typed);
            } else {
                auto value = std::invoke(function, typed);
                doNotOptimize(value);
            }
        };

        for (size_t n : config.sizes) {
            run_.setN(n);
            // A fixture that is expensive to create (GPU buffers, pipelines) may live for the
            // whole point: prepare() still resets its state before every iteration.
            if constexpr (reusesFixture<FixtureType>())
                shared_ = createFixture_(n);
            PointResult point = run(n, operations, invoke);
            shared_.reset();

            if (result_)
                result_(point);

            points.push_back(std::move(point));
        }

        if (complete_) {
            complete_({
                .name = name_,
                .group = group_,
                .points = points,
                .analysis = analysis.run(points),
                .implementation = variant_.implementation,
                .inputs = parameters(variant_.inputs),
                .params = parameters(variant_.params),
                .variant = variantName(),
            });
        }
    }

private:
    /// Phases — в стадию с Time (там же стена итерации), если её ещё нет ни в одной стадии.
    void addPhases() {
        analysis.phases(N);
        for (auto& stage : stages.data())
            for (auto& capability : stage.capabilities)
                if (capability->name() == Phases::capabilityName())
                    return;
        for (auto& stage : stages.data())
            for (auto& capability : stage.capabilities)
                if (capability->name() == Time::capabilityName()) {
                    stage.capabilities.push_back(std::make_unique<Phases>());
                    return;
                }
        stages.add<Time, Phases>().samples(config.samples);
    }

    template<typename T>
    static void doNotOptimize(const T& value) {
    #if defined(__GNUC__) || defined(__clang__)
        asm volatile("" : : "r,m"(value) : "memory");
    #else
        (void)value;
    #endif
    }

    /// `static constexpr bool Reuse = true` in a fixture: one fixture per point N, shared by
    /// calibration, stages and samples, instead of a new one per sample.
    template<typename FixtureType>
    static consteval bool reusesFixture() {
        if constexpr (requires { FixtureType::Reuse; })
            return static_cast<bool>(FixtureType::Reuse);
        else
            return false;
    }

    template<typename FixtureType>
    static consteval size_t operationsPerIteration() {
        if constexpr (requires { FixtureType::Steps; })
            return static_cast<size_t>(FixtureType::Steps);
        else
            return 1;
    }

    static void normalize(
        Metrics& metrics,
        size_t iterations,
        size_t operations
    ) {
        for (size_t i = 0; i < metrics.values.size(); ++i) {
            if (hasFlag(metrics.schema[i].flags, MetricFlags::PerIteration))
                metrics.values[i] /= static_cast<double>(iterations);
            if (hasFlag(metrics.schema[i].flags, MetricFlags::PerOperation))
                metrics.values[i] /= static_cast<double>(operations);
        }
    }

    template<typename Invoke>
    PointResult run(size_t n, size_t operations, Invoke& invoke) {
        PointResult point{
            .name = label_.empty() ? name_ : label_,
            .group = group_,
            .n = n
        };

        for (Stages::Stage& stage : stages.data())
            runStage(n, operations, stage, invoke, point);

        return point;
    }

    Clock::duration measureOverhead(Stages::Stage& stage) {
        constexpr size_t rounds = 16;
        constexpr size_t iterations = 1000;

        Clock::duration best = Clock::duration::max();

        for (size_t round = 0; round < rounds; ++round) {
            const auto started = Clock::now();

            for (size_t i = 0; i < iterations; ++i) {
                for (auto& capability : stage.capabilities)
                    capability->start();

                for (auto it = stage.capabilities.rbegin(); it != stage.capabilities.rend(); ++it)
                    (*it)->stop();
            }

            const auto elapsed = Clock::now() - started;
            const auto average = elapsed / static_cast<Clock::rep>(iterations);
            best = std::min(best, average);
        }

        return best;
    }

    template<typename Invoke>
    void runStage(
        size_t n,
        size_t operations,
        Stages::Stage& stage,
        Invoke& invoke,
        PointResult& point
    ) {
        if (!stage.sampleLimit && stage.timeLimit == Clock::duration::zero())
            throw Exception<Bench>("Stage '{}' has no execution limit", stageName(stage));

        for (auto& capability : stage.capabilities)
            capability->bind(trace_);

        const std::string name = stageName(stage);
        StageResult stageResult{
            .name = name,
            .samples = stage.sampleLimit,
        };

        for (const auto& capability : stage.capabilities) {
            if (!capability->available()) {
                stageResult.unavailable.push_back({
                    .capability = std::string(capability->name()),
                    .reason = std::string(capability->unavailableReason())
                });
            }
        }

        if (stageResult.unavailable.size() == stage.capabilities.size()) {
            point.stages.push_back(std::move(stageResult));
            return;
        }

        const auto overhead = measureOverhead(stage);
        const size_t iterations = calibrate(n, invoke);

        stageResult.overhead = std::chrono::duration<double, std::nano>(overhead).count();

        const auto started = Clock::now();
        size_t sample = 0;

        while (true) {
            auto owned = shared_ ? nullptr : createFixture_(n);
            Fixture* fixture = shared_ ? shared_.get() : owned.get();
            trace_.reset();

            for (auto& capability : stage.capabilities)
                capability->begin();

            for (size_t i = 0; i < iterations; ++i) {
                fixture->prepare();

                for (auto& capability : stage.capabilities)
                    capability->start();

                invoke(*fixture);

                for (auto it = stage.capabilities.rbegin(); it != stage.capabilities.rend(); ++it)
                    (*it)->stop();
            }

            ++sample;
            // Отложенные отчёты исполнителя — после итераций, вне замера.
            trace_.flush();

            SampleResult sampleResult{
                .name = label_.empty() ? std::string_view(name_) : std::string_view(label_),
                .group = group_,
                .stage = name,
                .n = n,
                .sample = sample,
                .samples = stage.sampleLimit,
                .iterations = iterations,
                .overhead = std::chrono::duration<double, std::nano>(overhead).count()
            };

            sampleResult.capabilities.reserve(stage.capabilities.size());

            for (auto it = stage.capabilities.rbegin(); it != stage.capabilities.rend(); ++it) {
                Metrics metrics = (*it)->end();
                normalize(metrics, iterations, operations);

                if (!metrics.values.empty()) {
                    sampleResult.capabilities.push_back({
                        .capability = (*it)->name(),
                        .metrics = std::move(metrics)
                    });
                }
            }

            if (sample_)
                sample_(sampleResult);
            stageResult.sampleMetrics.push_back(std::move(sampleResult.capabilities));

            const bool samplesReached =
                stage.sampleLimit && sample >= stage.sampleLimit;

            const bool timeReached =
                stage.timeLimit != Clock::duration::zero() &&
                Clock::now() - started >= stage.timeLimit;

            if (samplesReached || timeReached)
                break;
        }

        stageResult.sample = sample;

        stageResult.capabilities.reserve(stage.capabilities.size());

        for (auto& capability : stage.capabilities) {
            Metrics metrics = capability->result();
            normalize(metrics, iterations, operations);

            if (!metrics.values.empty()) {
                stageResult.capabilities.push_back({
                    .capability = capability->name(),
                    .metrics = std::move(metrics)
                });
            }
        }

        point.stages.push_back(std::move(stageResult));
    }

    template<typename Invoke>
    size_t calibrate(size_t n, Invoke& invoke) {
        auto owned = shared_ ? nullptr : createFixture_(n);
        Fixture* fixture = shared_ ? shared_.get() : owned.get();

        size_t iterations = 1;

        while (true) {
            Clock::duration elapsed{};

            for (size_t i = 0; i < iterations; ++i) {
                fixture->prepare();

                const auto start = Clock::now();
                invoke(*fixture);
                elapsed += Clock::now() - start;
            }

            if (elapsed >= config.target)
                return iterations;

            const double scale =
                static_cast<double>(config.target.count()) /
                static_cast<double>(
                    std::chrono::duration_cast<decltype(config.target)>(elapsed).count()
                );

            iterations = std::max(
                iterations + 1,
                static_cast<size_t>(iterations * scale)
            );
        }
    }

    static std::string stageName(const Stages::Stage& stage) {
        std::string result;

        for (const auto& capability : stage.capabilities) {
            if (!result.empty())
                result += '|';

            result += capability->name();
        }

        return result;
    }
};

}
