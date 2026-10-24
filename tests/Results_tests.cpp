#include <cmath>
#include <string>

#include <BmRunner/Benchmarks.hpp>
#include <BmRunner/Results.hpp>
#include <Lattice/Tools/Tests.hpp>

namespace Lattice {

namespace {

const Benchmarks::ResultRow* findRow(const Benchmarks::ResultTable& table, std::string_view bench,
                                     std::string_view metric, std::string_view n = {}) {
    for (const auto& row : table.rows)
        if (row.bench == bench && row.metric == metric && (n.empty() || row.n == n))
            return &row;
    return nullptr;
}

}

TEST(Results_RowsOfARun, Fixture,
    "Запуск по фильтру должен давать строки метрик с семплами, анализа и сравнения исполнителей.") {
    Benchmarks::disableCallbacks();
    const auto table = Benchmarks::runFiltered("TraceCompare/Spin*");

    // Метрика стадии с семплами: по одному значению на семпл.
    const auto* execute = findRow(table, "SpinCpu", "Phases.Execute", "1000");
    REQUIRE(execute != nullptr && execute->samples.size() == 5 && execute->stage == "Time|Phases");
    REQUIRE(execute->work == "Spin" && execute->executor == "cpu" && execute->mode == "-");
    // Ряд семплов без итоговой метрики: значение — медиана.
    const auto* time = findRow(table, "SpinGpu", "Time.time", "100");
    REQUIRE(time != nullptr && time->samples.size() == 5);
    // Сводки Time — статистики того же ряда: семплы общие.
    const auto* timeMedian = findRow(table, "SpinGpu", "Time.median", "100");
    REQUIRE(timeMedian != nullptr && timeMedian->samples == time->samples);
    // Анализ и сводка исполнителей — строки с n = '-'.
    const auto* per = findRow(table, "SpinCpu", "Phases.Execute(N).per");
    REQUIRE(per != nullptr && per->n == "-" && per->stage == "analysis" && std::abs(per->value - 100.0) < 10.0);
    const auto* crossover = findRow(table, "-", "crossover:cpu");
    REQUIRE(crossover != nullptr && crossover->executor == "gpu" && crossover->stage == "compare");

    bool unknown = false;
    try {
        Benchmarks::runFiltered("NoSuchBench*");
    } catch (const ExceptionBase&) {
        unknown = true;
    }
    REQUIRE(unknown);
    Benchmarks::resetCallbacks();
}

TEST(Results_TsvRoundTrip, Fixture,
    "TSV должен сохранять метаданные, строки и семплы и сортировать строки стабильно.") {
    Benchmarks::ResultTable table;
    table.meta = {{"suite", "Demo.bench"}, {"build", "release, gcc 15.2.1, x86_64"}};
    table.rows = {
        {.group = "G", .bench = "B", .work = "W", .executor = "gpu", .mode = "resident", .n = "1000",
         .stage = "Time", .metric = "Time.median", .value = 1234.5678, .samples = {1.5, 2.25, 1e9}, .measured = "r0002"},
        {.group = "G", .bench = "B", .work = "W", .executor = "gpu", .mode = "resident", .n = "10",
         .stage = "Time", .metric = "Time.median", .value = std::nan(""), .measured = "-"},
        {.group = "A", .bench = "-", .work = "W", .executor = "cpu", .mode = "-", .n = "-",
         .stage = "compare", .metric = "per", .value = 0.125, .measured = "r0001"},
    };
    const std::string text = Benchmarks::writeTsv(table);
    const auto parsed = Benchmarks::parseTsv(text);

    REQUIRE(parsed.meta.size() == 2 && *parsed.find("suite") == "Demo.bench");
    REQUIRE(parsed.rows.size() == 3);
    // Порядок: группа, затем n числом (10 раньше 1000).
    REQUIRE(parsed.rows[0].group == "A" && parsed.rows[1].n == "10" && parsed.rows[2].n == "1000");
    REQUIRE(std::isnan(parsed.rows[1].value) && parsed.rows[1].samples.empty());
    REQUIRE(std::abs(parsed.rows[2].value - 1234.5678) < 1e-3 && parsed.rows[2].samples.size() == 3);
    REQUIRE(parsed.rows[2].samples[2] == 1e9 && parsed.rows[2].measured == "r0002");
    REQUIRE(Benchmarks::writeTsv(parsed) == text);

    bool broken = false;
    try {
        Benchmarks::parseTsv("group\tbench\n");
    } catch (const ExceptionBase&) {
        broken = true;
    }
    REQUIRE(broken);
}

}
