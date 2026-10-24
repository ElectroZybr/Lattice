#include "Benchmarks.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <utility>

#include <BmRunner/Output.hpp>
#include <BmRunner/Results.hpp>
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
    std::vector<std::string> swept;   // axes of --sweep
};

/// Полное время варианта работы по N — для сводки сравнения.
struct Record {
    std::string group;
    std::string work;
    std::string variant;
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
std::vector<Comparison> compare(const std::vector<Record>& records) {
    std::vector<Comparison> comparisons;
    std::vector<bool> done(records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        if (done[i])
            continue;
        Comparison comparison{
            .group = records[i].group,
            .work = records[i].work,
        };
        for (size_t j = i; j < records.size(); ++j) {
            const Record& record = records[j];
            if (record.group != records[i].group || record.work != records[i].work)
                continue;
            done[j] = true;
            const AffineFit fit = fitAffine(record.total);
            if (fit.valid)
                comparison.executors.push_back({record.variant, fit.fixed, fit.per, fit.error});
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
        comparisons.push_back(std::move(comparison));
    }
    return comparisons;
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

void execute(const Case& benchCase, std::vector<Record>* records = nullptr, ResultTable* table = nullptr) {
    State& s = state();

    const CompleteCallback complete = [&](const BenchResult& result) {
        if (s.complete)
            s.complete(result);
        if (table)
            for (auto& row : resultRows(result))
                table->rows.push_back(std::move(row));
        if (records && !result.implementation.empty())
            records->push_back({
                .group = result.group,
                .work = result.name,
                .variant = result.label(),
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

namespace {

/// Маска с `*` (любая строка) и `?` (один символ).
bool glob(std::string_view mask, std::string_view text) {
    if (mask.empty())
        return text.empty();
    if (mask.front() == '*')
        return glob(mask.substr(1), text) || (!text.empty() && glob(mask, text.substr(1)));
    return !text.empty() && (mask.front() == '?' || mask.front() == text.front()) &&
           glob(mask.substr(1), text.substr(1));
}

bool matches(const Case& bench, std::string_view filter) {
    if (filter.empty() || filter == bench.group || filter == bench.name)
        return true;
    return glob(filter, bench.group.empty() ? bench.name : bench.group + "/" + bench.name);
}

}

ResultTable runFiltered(std::string_view filter) {
    State& s = state();
    ResultTable table;
    std::vector<Record> records;
    size_t matched = 0;

    beginSession();
    for (BenchId id = 0; id < s.benches.size(); ++id) {
        const Case* bench = s.benches.get(id);
        if (bench && matches(*bench, filter)) {
            ++matched;
            execute(*bench, &records, &table);
        }
    }
    if (!matched)
        throw Exception<Registry>("no benchmark matches '{}'", filter);

    for (const Comparison& comparison : compare(records)) {
        if (s.comparison)
            s.comparison(comparison);
        for (auto& row : resultRows(comparison))
            table.rows.push_back(std::move(row));
    }
    return table;
}

void runGroup(std::string_view group) {
    for (const auto& known : groups())
        if (known == group) {
            runFiltered(group);
            return;
        }
    throw Exception<Registry>("Benchmark group '{}' not found", group);
}

void runAll() {
    runFiltered({});
}

bool sweeping(std::string_view axis) {
    const auto& swept = state().swept;
    return std::ranges::find(swept, axis) != swept.end();
}

int runMain(int argc, char** argv, std::string_view build) {
    std::string filter;
    std::string tsv;
    bool listOnly = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc)
                throw Exception<Registry>("{} needs a value", argument);
            return argv[++i];
        };
        if (argument == "--list")
            listOnly = true;
        else if (argument == "--filter")
            filter = value();
        else if (argument == "--tsv")
            tsv = value();
        else if (argument == "--sweep")
            state().swept.push_back(value());
        else if (argument == "--help" || argument == "-h") {
            Logger::message(TextFormatter::format(
                "usage: {} [filter] [--filter <group|bench|mask>] [--tsv <file>] [--sweep <axis>] [--list]", argv[0]));
            return 0;
        } else if (!argument.starts_with("--") && filter.empty())
            filter = std::string(argument);
        else
            throw Exception<Registry>("unknown argument '{}'", argument);
    }

    if (listOnly) {
        for (const Info& info : list())
            LogSystem::writeConsole(std::format("{}\t{}\t{}\n",
                info.group.empty() ? std::string(info.name) : std::format("{}/{}", info.group, info.name),
                info.group, info.description));
        return 0;
    }

    ResultTable table = runFiltered(filter);
    if (!tsv.empty()) {
        std::string suite = argc ? argv[0] : "suite";
        if (const auto slash = suite.find_last_of("/\\"); slash != std::string::npos)
            suite = suite.substr(slash + 1);
        table.meta = {{"suite", suite}, {"build", std::string(build)}, {"filter", filter.empty() ? "*" : filter}};
        std::ofstream out(tsv, std::ios::binary);
        if (!out)
            throw Exception<Registry>("cannot write '{}'", tsv);
        out << writeTsv(table);
    }
    return 0;
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
