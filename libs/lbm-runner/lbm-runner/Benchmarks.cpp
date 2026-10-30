#include "Benchmarks.hpp"
#include "Console.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <utility>

#include <lbm-runner/Output.hpp>
#include <lbm-runner/Results.hpp>
#include <lattice-sysinfo/Output.hpp>
#include <Lattice/tools/ObjectRegistry.hpp>
#include <Lattice/tools/Exception.hpp>

namespace Lattice::Benchmarks {

namespace {

using BenchId = uint32_t;

struct Registry {};

struct Case {
    std::string group;
    std::string name;
    std::string description;
    std::function<void(Bench&)> function;
    Bench::FixtureFactory createFixture;
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
    std::map<std::string, std::vector<std::string>, std::less<>> axes;   // --axis name=v1,v2
    std::vector<WorkRegistration> works;
    std::vector<ImplementationRegistration> implementations;
};

/// Полное время варианта работы по N — для сводки сравнения.
struct Record {
    std::string group;
    std::string work;
    std::string inputs;
    std::string implementation;
    std::string params;
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
            .inputs = records[i].inputs,
        };
        for (size_t j = i; j < records.size(); ++j) {
            const Record& record = records[j];
            if (record.group != records[i].group || record.work != records[i].work || record.inputs != records[i].inputs)
                continue;
            done[j] = true;
            const AffineFit fit = fitAffine(record.total);
            if (fit.valid)
                comparison.executors.push_back({
                    .executor = record.variant,
                    .implementation = record.implementation,
                    .params = record.params,
                    .fixed = fit.fixed,
                    .per = fit.per,
                    .error = fit.error,
                });
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

/// Работа и её реализации из всех единиц трансляции: эталон первым, затем по имени.
void runWork(Bench& bench, const WorkRegistration& registration) {
    State& s = state();
    std::vector<const ImplementationRegistration*> implementations;
    for (const auto& implementation : s.implementations) {
        if (implementation.group != registration.group || implementation.work != registration.name)
            continue;
        for (const auto* other : implementations)
            if (other->name == implementation.name)
                throw Exception<Registry>("'{}/{}' has two implementations '{}': {} and {}", registration.group,
                    registration.name, implementation.name, other->where, implementation.where);
        if (implementation.inputType != registration.inputType)
            throw Exception<Registry>("'{}/{}/{}' ({}) takes {}, the work ({}) gives {}", registration.group,
                registration.name, implementation.name, implementation.where, implementation.inputType,
                registration.where, registration.inputType);
        implementations.push_back(&implementation);
    }
    if (implementations.empty())
        throw Exception<Registry>("work '{}/{}' ({}) has no implementations: add BENCH_IMPLEMENTATION",
            registration.group, registration.name, registration.where);

    std::unique_ptr<WorkBase> work = registration.create(bench);
    const std::string& reference = work->referenceName();
    if (!reference.empty() && std::ranges::none_of(implementations, [&](const auto* i) { return i->name == reference; }))
        throw Exception<Registry>("work '{}/{}': the reference '{}' is not registered", registration.group,
            registration.name, reference);
    std::ranges::sort(implementations, [&](const auto* a, const auto* b) {
        if ((a->name == reference) != (b->name == reference))
            return a->name == reference;
        return a->name < b->name;
    });
    for (const auto* implementation : implementations)
        implementation->run(bench, *work, *implementation);
}

/// Реализации без своей работы — ошибка, когда фильтр выбирает их работу.
void checkOrphans(std::string_view filter);

void execute(const Case& benchCase, std::vector<Record>* records = nullptr, ResultTable* table = nullptr,
             Output::Console* console = nullptr) {
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
                .inputs = inputLabel(result.inputs),
                .implementation = result.implementation,
                .params = parametersText(result.params),
                .variant = result.label(),
                .total = totalTime(result)
            });
    };

    const SampleCallback sample = [&](const SampleResult& result) {
        if (console) console->sample(result);
        if (s.sample) s.sample(result);
    };
    Bench bench(
        benchCase.group,
        benchCase.name,
        benchCase.createFixture,
        sample,
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

void registerWork(WorkRegistration registration) {
    State& s = state();
    const std::string group = registration.group, name = registration.name, description = registration.description;
    const size_t index = s.works.size();
    s.works.push_back(std::move(registration));
    add({group, name, description, [index](Bench& bench) { runWork(bench, state().works[index]); }, nullptr});
}

void registerImplementation(ImplementationRegistration registration) {
    state().implementations.push_back(std::move(registration));
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

namespace {

void checkOrphans(std::string_view filter) {
    State& s = state();
    for (const auto& implementation : s.implementations) {
        const Case probe{implementation.group, implementation.work, {}, {}, nullptr};
        if (!matches(probe, filter))
            continue;
        if (std::ranges::none_of(s.works, [&](const WorkRegistration& work) {
                return work.group == implementation.group && work.name == implementation.work;
            }))
            throw Exception<Registry>("implementation '{}/{}/{}' ({}) has no work: declare BENCH_WORK({}, {}, …)",
                implementation.group, implementation.work, implementation.name, implementation.where,
                implementation.group, implementation.work);
    }
}

}

namespace {
ResultTable runSelected(std::string_view filter, Output::Mode mode) {
    State& s = state();
    ResultTable table;
    std::vector<Record> records;

    checkOrphans(filter);
    size_t selected = 0;
    for (BenchId id = 0; id < s.benches.size(); ++id)
        if (const auto* bench = s.benches.get(id); bench && matches(*bench, filter)) ++selected;
    if (!selected) throw Exception<Registry>("no benchmark matches '{}'", filter);
    Output::Console console(mode, selected);
    console.start("system info");
    beginSession();
    for (BenchId id = 0; id < s.benches.size(); ++id) {
        const Case* bench = s.benches.get(id);
        if (bench && matches(*bench, filter)) {
            console.start(Output::fullName(bench->group, bench->name));
            execute(*bench, &records, &table, &console);
            console.advance();
        }
    }
    for (const Comparison& comparison : compare(records)) {
        if (s.comparison)
            s.comparison(comparison);
        for (auto& row : resultRows(comparison))
            table.rows.push_back(std::move(row));
    }
    console.finish();
    return table;
}
}

ResultTable runFiltered(std::string_view filter) { return runSelected(filter, Output::Mode::Text); }

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

const std::vector<std::string>* axisOverride(std::string_view axis) {
    const auto& axes = state().axes;
    const auto it = axes.find(axis);
    return it != axes.end() ? &it->second : nullptr;
}

namespace {

int runSuite(int argc, char** argv, std::string_view build);

}

int runMain(int argc, char** argv, std::string_view build) {
    // A failed check or a broken registration is reported, not std::terminate.
    try {
        return runSuite(argc, argv, build);
    } catch (const std::exception& error) {
        Logger::error("bench", "{}", error.what());
        return 1;
    }
}

namespace {

int runSuite(int argc, char** argv, std::string_view build) {
    std::string filter;
    std::string tsv;
    bool listOnly = false;
    unsigned verbosity = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc)
                throw Exception<Registry>("{} needs a value", argument);
            return argv[++i];
        };
        if (argument == "-v" || argument == "--verbose")
            verbosity = std::min(2u, verbosity + 1);
        else if (argument == "-vv")
            verbosity = 2;
        else if (argument == "--list")
            listOnly = true;
        else if (argument == "--filter")
            filter = value();
        else if (argument == "--tsv")
            tsv = value();
        else if (argument == "--sweep")
            state().swept.push_back(value());
        else if (argument == "--axis") {
            // --axis workers=4,8
            const std::string given = value();
            const size_t equals = given.find('=');
            if (equals == std::string::npos || equals == 0 || equals + 1 == given.size())
                throw Exception<Registry>("--axis needs <axis>=<v1,v2,…>, got '{}'", given);
            std::vector<std::string> values;
            for (size_t at = equals + 1; at <= given.size();) {
                const size_t comma = std::min(given.find(',', at), given.size());
                values.push_back(given.substr(at, comma - at));
                at = comma + 1;
            }
            state().axes[given.substr(0, equals)] = std::move(values);
        }
        else if (argument == "--help" || argument == "-h") {
            Logger::message(TextFormatter::format(
                "usage: {} [filter] [--filter <group|bench|mask>] [--tsv <file>] [--sweep <axis>] [--axis <axis>=<v1,v2>] [--list] [-v|-vv]\n"
                "  default: progress only; -v: text + progress; -vv: legacy text", argv[0]));
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

    ResultTable table = runSelected(filter, verbosity == 0 ? Output::Mode::Progress :
        verbosity == 1 ? Output::Mode::ProgressAndText : Output::Mode::Text);
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
