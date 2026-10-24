#include <cmath>
#include <string>

#include <BmRunner/Benchmarks.hpp>
#include <BmRunner/Results.hpp>
#include <Lattice/Tools/Tests.hpp>

namespace Lattice {

namespace {

const Benchmarks::ResultRow* findRow(const Benchmarks::ResultTable& table, std::string_view implementation,
                                     std::string_view metric, std::string_view n = {}) {
    for (const auto& row : table.rows)
        if (row.implementation == implementation && row.metric == metric && (n.empty() || row.n == n))
            return &row;
    return nullptr;
}

}

TEST(Results_RowsOfARun, Fixture,
    "Запуск по фильтру должен давать строки метрик с семплами, анализа и сравнения исполнителей.") {
    Benchmarks::disableCallbacks();
    const auto table = Benchmarks::runFiltered("TraceCompare/Spin");

    // Метрика стадии с семплами: по одному значению на семпл.
    const auto* execute = findRow(table, "cpu", "Phases.Execute", "1000");
    REQUIRE(execute != nullptr && execute->samples.size() == 5 && execute->stage == "Time|Phases");
    REQUIRE(execute->bench == "Spin" && execute->params == "workers=1" && execute->variant == "cpu");
    REQUIRE(execute->unit == "ns");
    // Ряд семплов без итоговой метрики: значение — медиана.
    const auto* time = findRow(table, "gpu", "Time.time", "100");
    REQUIRE(time != nullptr && time->samples.size() == 5);
    // Сводки Time — статистики того же ряда: семплы общие.
    const auto* timeMedian = findRow(table, "gpu", "Time.median", "100");
    REQUIRE(timeMedian != nullptr && timeMedian->samples == time->samples);
    // Анализ и сводка исполнителей — строки с n = '-'.
    const auto* per = findRow(table, "cpu", "Phases.Execute(N).per");
    REQUIRE(per != nullptr && per->n == "-" && per->stage == "analysis" && std::abs(per->value - 100.0) < 10.0);
    const auto* crossover = findRow(table, "gpu", "crossover:cpu");
    REQUIRE(crossover != nullptr && crossover->bench == "Spin" && crossover->stage == "compare");
    REQUIRE(crossover->variant == "gpu" && crossover->unit == "count");
    // Сводка варианта с осью: реализация и параметры отдельно, имя варианта — готовое.
    bool two = false;
    for (const auto& row : table.rows)
        if (row.stage == "compare" && row.metric == "per" && row.variant == "cpu×2")
            two = row.implementation == "cpu" && row.params == "workers=2" && row.unit == "ns";
    REQUIRE(two);

    bool unknown = false;
    try {
        Benchmarks::runFiltered("NoSuchBench*");
    } catch (const ExceptionBase&) {
        unknown = true;
    }
    REQUIRE(unknown);
    Benchmarks::resetCallbacks();
}

TEST(Results_ReadsTheFirstFormat, Fixture,
    "Таблица первого формата (work/executor/mode) должна читаться как варианты одной работы.") {
    const auto table = Benchmarks::parseTsv(
        "group\tbench\twork\texecutor\tmode\tn\tstage\tmetric\tvalue\tsamples\tmeasured\n"
        "Walls\tConstrainGpu\tConstrain\tgpu\tresident\t1000\tTime\tTime.median\t5\t4,5,6\tr0001\n"
        "Walls\tConstrainCold\tConstrain\tcpu\toffload\t1000\tTime\tTime.median\t7\t-\tr0001\n"
        "Space\tWrapColumns\tWrapColumns\t-\t-\t1000\tTime\tTime.median\t2\t-\tr0001\n");
    REQUIRE(table.rows.size() == 3);
    const auto& gpu = table.rows[0];
    REQUIRE(gpu.bench == "Constrain" && gpu.implementation == "gpu" && gpu.params == "-" && gpu.samples.size() == 3);
    const auto& cold = table.rows[1];
    REQUIRE(cold.bench == "ConstrainOffload" && cold.implementation == "cpu" && cold.params == "workers=1");
    REQUIRE(cold.variant == "cpu" && cold.unit == "ns" && gpu.variant == "gpu" && table.rows[2].variant == "-");
    REQUIRE(table.rows[2].bench == "WrapColumns" && table.rows[2].implementation == "-" && table.rows[2].measured == "r0001");
}

TEST(Results_ReadsTheSecondFormat, Fixture,
    "Таблица без variant и unit должна получать их по прежним правилам, сводка — реализацию и параметры.") {
    const auto table = Benchmarks::parseTsv(
        "group\tbench\timplementation\tparams\tn\tstage\tmetric\tvalue\tsamples\tmeasured\n"
        "Walls\tSoftForces\tcpu\tworkers=16\t1000\tTime|Phases\tPhases.Transfer.In.rate\t5\t-\tr0001\n"
        "Walls\tSoftForces\tcpu×16\t-\t-\tcompare\tper\t2\t-\tr0001\n"
        "Walls\tSoftForces\tgpu\t-\t-\tanalysis\tTime.median(N).error\t3\t-\tr0001\n");
    REQUIRE(table.rows.size() == 3);
    const auto& rate = table.rows[0];
    REQUIRE(rate.variant == "cpu×16" && rate.unit == "B/s" && Benchmarks::higherIsBetter(rate));
    const auto& per = table.rows[1];
    REQUIRE(per.variant == "cpu×16" && per.implementation == "cpu" && per.params == "workers=16" && per.unit == "ns");
    REQUIRE(table.rows[2].variant == "gpu" && table.rows[2].unit == "%");
}

TEST(Results_TsvRoundTrip, Fixture,
    "TSV должен сохранять метаданные, строки и семплы и сортировать строки стабильно.") {
    Benchmarks::ResultTable table;
    table.meta = {{"suite", "Demo.bench"}, {"build", "release, gcc 15.2.1, x86_64"}};
    table.rows = {
        {.group = "G", .bench = "B", .implementation = "gpu", .params = "-", .variant = "gpu", .n = "1000",
         .stage = "Time", .metric = "Time.median", .unit = "ns", .value = 1234.5678, .samples = {1.5, 2.25, 1e9}, .measured = "r0002"},
        {.group = "G", .bench = "B", .implementation = "gpu", .params = "-", .n = "10",
         .stage = "Time", .metric = "Time.median", .value = std::nan(""), .measured = "-"},
        {.group = "A", .bench = "W", .implementation = "cpu", .params = "workers=16", .variant = "cpu×16", .n = "-",
         .stage = "compare", .metric = "per", .unit = "ns", .value = 0.125, .measured = "r0001"},
    };
    const std::string text = Benchmarks::writeTsv(table);
    const auto parsed = Benchmarks::parseTsv(text);

    REQUIRE(parsed.meta.size() == 2 && *parsed.find("suite") == "Demo.bench");
    REQUIRE(parsed.rows.size() == 3);
    // Порядок: группа, затем n числом (10 раньше 1000).
    REQUIRE(parsed.rows[0].group == "A" && parsed.rows[1].n == "10" && parsed.rows[2].n == "1000");
    REQUIRE(std::isnan(parsed.rows[1].value) && parsed.rows[1].samples.empty());
    REQUIRE(std::abs(parsed.rows[2].value - 1234.5678) < 1e-3 && parsed.rows[2].samples.size() == 3);
    REQUIRE(parsed.rows[2].samples[2] == 1e9 && parsed.rows[2].measured == "r0002" && parsed.rows[2].unit == "ns");
    REQUIRE(parsed.rows[0].variant == "cpu×16" && parsed.rows[1].variant == "-" && parsed.rows[1].unit == "-");
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
