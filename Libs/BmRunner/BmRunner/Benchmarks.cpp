#include "Benchmarks.hpp"

#include <algorithm>
#include <utility>

#include <BmRunner/Output.hpp>
#include <SysInfo/Output.hpp>
#include <Lattice/Tools/ObjectRegistry.hpp>
#include <Lattice/Tools/Exception.hpp>

namespace Lattice::Benchmarks {

namespace {

using BenchId = uint32_t;

struct Registry {};

struct Case {
    std::string group;
    std::string name;
    std::string description;
    void (*function)(Bench&);
    Fixture::Factory createFixture;
};

struct GroupConfig {
    std::string name;
    void (*function)(Bench&);
};

struct State {
    ObjectRegistry<Case, BenchId, std::string> benches;
    std::vector<GroupConfig> groups;

    SampleCallback sample = Output::sample;
    ResultCallback result = Output::result;
    CompleteCallback complete = Output::complete;
    ComparisonCallback comparison = Output::comparison;
};

/// Полное время работы на исполнителе по N — для сводки сравнения.
struct Record {
    std::string group;
    std::string work;
    Mode mode;
    std::string executor;
    std::vector<ComplexityPoint> total;
};

std::vector<ComplexityPoint> totalTime(const BenchResult& result) {
    std::vector<ComplexityPoint> points;
    for (const PointResult& point : result.points)
        for (const StageResult& stage : point.stages)
            for (const CapabilityMetrics& capability : stage.capabilities) {
                if (capability.capability != Time::capabilityName())
                    continue;
                const auto& metrics = capability.metrics;
                for (size_t i = 0; i < metrics.schema.size() && i < metrics.values.size(); ++i)
                    if (metrics.schema[i].name == "median")
                        points.push_back({.x = double(point.n), .y = metrics.values[i]});
            }
    return points;
}

/// Работы, измеренные на двух и более исполнителях: подгонка каждой и пороги N*.
void compare(const std::vector<Record>& records, const ComparisonCallback& callback) {
    if (!callback)
        return;
    std::vector<bool> done(records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        if (done[i])
            continue;
        Comparison comparison{
            .group = records[i].group,
            .work = records[i].work,
            .mode = std::string(modeName(records[i].mode))
        };
        for (size_t j = i; j < records.size(); ++j) {
            const Record& record = records[j];
            if (record.group != records[i].group || record.work != records[i].work || record.mode != records[i].mode)
                continue;
            done[j] = true;
            const AffineFit fit = fitAffine(record.total);
            if (fit.valid)
                comparison.executors.push_back({record.executor, fit.fixed, fit.per, fit.error});
        }
        if (comparison.executors.size() < 2)
            continue;

        const auto& fits = comparison.executors;
        for (size_t a = 0; a < fits.size(); ++a)
            for (size_t b = a + 1; b < fits.size(); ++b) {
                if (fits[a].per == fits[b].per)
                    continue;
                // Выигрывает на больших N тот, у кого меньше цена на элемент; порог —
                // пересечение прямых (0, если он быстрее везде).
                const auto& fast = fits[a].per < fits[b].per ? fits[a] : fits[b];
                const auto& slow = fits[a].per < fits[b].per ? fits[b] : fits[a];
                const double n = std::max(0.0, (fast.fixed - slow.fixed) / (slow.per - fast.per));
                comparison.crossovers.push_back({fast.executor, slow.executor, n});
            }
        callback(comparison);
    }
}

State& state() {
    static State value;
    return value;
}

void add(Case bench) {
    state().benches.create(std::move(bench));
}

void addGroupConfig(GroupConfig config) {
    auto& groups = state().groups;

    if (std::ranges::find(groups, config.name, &GroupConfig::name) != groups.end())
        throw Exception<Registry>("Benchmark group '{}' is already configured", config.name);

    groups.push_back(std::move(config));
}

const GroupConfig* findGroupConfig(std::string_view group) {
    auto& groups = state().groups;
    const auto it = std::ranges::find(groups, group, &GroupConfig::name);
    return it != groups.end() ? &*it : nullptr;
}

void execute(const Case& benchCase, std::vector<Record>* records = nullptr) {
    State& s = state();

    const CompleteCallback complete = [&](const BenchResult& result) {
        if (s.complete)
            s.complete(result);
        if (records && !result.executor.empty())
            records->push_back({
                .group = std::string(result.group),
                .work = result.work,
                .mode = result.mode,
                .executor = result.executor,
                .total = totalTime(result)
            });
    };

    Bench bench(
        benchCase.group,
        benchCase.name,
        benchCase.createFixture,
        s.sample,
        s.result,
        complete
    );

    if (const GroupConfig* config = findGroupConfig(benchCase.group))
        config->function(bench);

    benchCase.function(bench);
}

}

Registrar::Registrar(
    std::string_view group,
    std::string_view name,
    std::string_view description,
    void (*function)(Bench&),
    Fixture::Factory createFixture
) {
    add({
        std::string(group),
        std::string(name),
        std::string(description),
        function,
        createFixture
    });
}

GroupRegistrar::GroupRegistrar(std::string_view group, void (*function)(Bench&)) {
    addGroupConfig({
        .name = std::string(group),
        .function = function
    });
}

void beginSession() {
    SystemInfo::Output::print();
}

void run(std::string_view name) {
    State& s = state();
    const BenchId id = s.benches.find(name);

    if (!s.benches.valid(id))
        throw Exception<Registry>("Benchmark '{}' not found", name);

    beginSession();
    execute(s.benches.require(id));
}

void runGroup(std::string_view group) {
    State& s = state();

    beginSession();
    std::vector<Record> records;
    for (BenchId id = 0; id < s.benches.size(); ++id) {
        const Case* bench = s.benches.get(id);

        if (bench && bench->group == group)
            execute(*bench, &records);
    }
    compare(records, s.comparison);
}

void runAll() {
    State& s = state();

    beginSession();
    std::vector<Record> records;
    for (BenchId id = 0; id < s.benches.size(); ++id)
        if (const Case* bench = s.benches.get(id))
            execute(*bench, &records);
    compare(records, s.comparison);
}

std::vector<Info> list() {
    State& s = state();
    std::vector<Info> result;

    for (BenchId id = 0; id < s.benches.size(); ++id) {
        const Case* bench = s.benches.get(id);
        if (!bench)
            continue;

        result.push_back({
            .group = bench->group,
            .name = bench->name,
            .description = bench->description
        });
    }

    return result;
}

std::vector<std::string_view> groups() {
    State& s = state();
    std::vector<std::string_view> result;

    for (BenchId id = 0; id < s.benches.size(); ++id) {
        const Case* bench = s.benches.get(id);

        if (!bench || bench->group.empty())
            continue;

        if (std::ranges::find(result, bench->group) == result.end())
            result.push_back(bench->group);
    }

    return result;
}

void setSampleCallback(SampleCallback callback) {
    state().sample = std::move(callback);
}

void setResultCallback(ResultCallback callback) {
    state().result = std::move(callback);
}

void setCompleteCallback(CompleteCallback callback) {
    state().complete = std::move(callback);
}

void setComparisonCallback(ComparisonCallback callback) {
    state().comparison = std::move(callback);
}

void disableComparisonCallback() {
    state().comparison = {};
}

void disableSampleCallback() {
    state().sample = {};
}

void disableResultCallback() {
    state().result = {};
}

void disableCompleteCallback() {
    state().complete = {};
}

void disableCallbacks() {
    State& s = state();
    s.sample = {};
    s.result = {};
    s.complete = {};
    s.comparison = {};
}

void resetCallbacks() {
    State& s = state();
    s.sample = Output::sample;
    s.result = Output::result;
    s.complete = Output::complete;
    s.comparison = Output::comparison;
}

}
