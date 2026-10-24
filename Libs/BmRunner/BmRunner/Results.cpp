#include "Results.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <variant>

#include <Lattice/Tools/Exception.hpp>

namespace Lattice::Benchmarks {

namespace {

struct ResultsFormat;
using Error = Exception<ResultsFormat>;

constexpr std::string_view Header =
    "group\tbench\timplementation\tparams\tn\tstage\tmetric\tvalue\tsamples\tmeasured";
constexpr size_t Columns = 10;

// The first format (one bench per implementation, tagged with work/executor/mode): read and
// mapped onto the current one, so saved history keeps comparing with new runs.
constexpr std::string_view LegacyHeader =
    "group\tbench\twork\texecutor\tmode\tn\tstage\tmetric\tvalue\tsamples\tmeasured";
constexpr size_t LegacyColumns = 11;

/// Поля без табуляций и переводов строк; пустое — `-`.
std::string field(std::string_view value) {
    if (value.empty())
        return "-";
    std::string result(value);
    std::ranges::replace(result, '\t', ' ');
    std::ranges::replace(result, '\n', ' ');
    return result;
}

std::string number(double value) {
    if (std::isnan(value))
        return "nan";
    if (std::isinf(value))
        return value > 0 ? "inf" : "-inf";
    return std::format("{:.7g}", value);
}

double parseNumber(std::string_view text, size_t line) {
    if (text == "nan")
        return std::nan("");
    if (text == "inf")
        return INFINITY;
    if (text == "-inf")
        return -INFINITY;
    double value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        throw Error("line {}: '{}' is not a number", line, text);
    return value;
}

std::string metricName(std::string_view capability, std::string_view name) {
    return std::format("{}.{}", capability, name);
}

std::string refName(const ValueRef& ref) {
    if (ref.source == ValueSource::Parameter)
        return std::string(ref.name.empty() ? "N" : ref.name);
    return metricName(ref.capability, ref.name);
}

ResultRow base(const BenchResult& result) {
    return {
        .group = field(result.group),
        .bench = field(result.name),
        .implementation = field(result.implementation),
        .params = field(parametersText(result.params)),
        .measured = "-",
    };
}

double median(std::vector<double> values) {
    if (values.empty())
        return std::nan("");
    std::ranges::sort(values);
    return values[values.size() / 2];
}

}

std::vector<ResultRow> resultRows(const BenchResult& result) {
    std::vector<ResultRow> rows;
    for (const PointResult& point : result.points) {
        for (const StageResult& stage : point.stages) {
            // Ряды семплов по имени метрики.
            std::map<std::string, std::vector<double>> series;
            for (const auto& sample : stage.sampleMetrics)
                for (const CapabilityMetrics& capability : sample) {
                    const auto& metrics = capability.metrics;
                    for (size_t i = 0; i < metrics.schema.size() && i < metrics.values.size(); ++i)
                        series[metricName(capability.capability, metrics.schema[i].name)].push_back(metrics.values[i]);
                }

            auto row = [&](std::string metric, double value, std::vector<double> samples) {
                ResultRow next = base(result);
                next.n = std::to_string(point.n);
                next.stage = field(stage.name);
                next.metric = field(metric);
                next.value = value;
                next.samples = std::move(samples);
                rows.push_back(std::move(next));
            };

            for (const CapabilityMetrics& capability : stage.capabilities) {
                // Сводки capability с одним рядом семплов (Time: min, median, mean по
                // `time`) — статистики этого ряда: семплы у них общие.
                const std::string prefix = std::string(capability.capability) + ".";
                const std::vector<double>* only = nullptr;
                size_t count = 0;
                for (const auto& [name, values] : series)
                    if (name.starts_with(prefix)) {
                        only = &values;
                        ++count;
                    }
                const auto& metrics = capability.metrics;
                std::vector<std::string> matched;
                for (size_t i = 0; i < metrics.schema.size() && i < metrics.values.size(); ++i) {
                    std::string name = metricName(capability.capability, metrics.schema[i].name);
                    std::vector<double> samples;
                    if (auto it = series.find(name); it != series.end()) {
                        samples = it->second;
                        matched.push_back(name);
                    } else if (count == 1) {
                        samples = *only;
                    }
                    row(std::move(name), metrics.values[i], std::move(samples));
                }
                for (const auto& name : matched)
                    series.erase(name);
            }
            // Ряды без итоговой метрики (Time.time): значение — медиана семплов.
            for (auto& [name, samples] : series) {
                const double value = median(samples);
                row(name, value, std::move(samples));
            }
        }
    }

    for (const AnalysisResult& analysis : result.analysis) {
        const std::string expression = std::format("{}({})", refName(analysis.y), refName(analysis.x));
        for (const AnalysisValue& value : analysis.values) {
            const double* number = std::get_if<double>(&value.value);
            const uint64_t* count = std::get_if<uint64_t>(&value.value);
            if (!number && !count)
                continue;   // строки (имя сложности) — не числа
            ResultRow next = base(result);
            next.n = "-";
            next.stage = "analysis";
            next.metric = field(std::format("{}.{}", expression, value.name.empty() ? "value" : value.name));
            next.value = number ? *number : double(*count);
            rows.push_back(std::move(next));
        }
    }
    return rows;
}

std::vector<ResultRow> resultRows(const Comparison& comparison) {
    std::vector<ResultRow> rows;
    auto row = [&](std::string_view executor, std::string metric, double value) {
        rows.push_back({
            .group = field(comparison.group),
            .bench = field(comparison.work),
            .implementation = field(executor),
            .params = "-",
            .n = "-",
            .stage = "compare",
            .metric = field(metric),
            .value = value,
            .measured = "-",
        });
    };
    for (const ExecutorFit& fit : comparison.executors) {
        row(fit.executor, "fixed", fit.fixed);
        row(fit.executor, "per", fit.per);
        row(fit.executor, "error", fit.error);
    }
    for (const Crossover& crossover : comparison.crossovers)
        row(crossover.faster, "crossover:" + crossover.slower, crossover.n);
    return rows;
}

std::string rowKey(const ResultRow& row) {
    return std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}", row.group, row.bench, row.implementation, row.params, row.n,
        row.stage, row.metric);
}

void sortRows(std::vector<ResultRow>& rows) {
    auto numeric = [](const std::string& n) {
        double value = 0;
        const auto [end, error] = std::from_chars(n.data(), n.data() + n.size(), value);
        return error == std::errc{} && end == n.data() + n.size() ? value : INFINITY;
    };
    std::ranges::stable_sort(rows, [&](const ResultRow& a, const ResultRow& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.bench != b.bench) return a.bench < b.bench;
        if (a.implementation != b.implementation) return a.implementation < b.implementation;
        if (a.params != b.params) return a.params < b.params;
        if (const double x = numeric(a.n), y = numeric(b.n); x != y) return x < y;
        if (a.stage != b.stage) return a.stage < b.stage;
        return a.metric < b.metric;
    });
}

std::string writeTsv(const ResultTable& table) {
    std::string text;
    for (const auto& [key, value] : table.meta)
        text += std::format("# {}: {}\n", field(key), field(value));
    text += Header;
    text += '\n';

    std::vector<ResultRow> rows = table.rows;
    sortRows(rows);
    for (const ResultRow& row : rows) {
        std::string samples;
        for (size_t i = 0; i < row.samples.size(); ++i)
            samples += (i ? "," : "") + number(row.samples[i]);
        text += std::format("{}\t{}\t{}\t{}\n", rowKey(row), number(row.value), samples.empty() ? "-" : samples,
            field(row.measured));
    }
    return text;
}

ResultTable parseTsv(std::string_view text) {
    ResultTable table;
    bool header = false;
    bool legacy = false;
    std::vector<std::string> migrated;   // storage of the mapped legacy fields
    size_t line = 0;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        std::string_view current = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++line;
        if (!current.empty() && current.back() == '\r')
            current.remove_suffix(1);
        if (current.empty())
            continue;

        if (current.starts_with('#')) {
            if (header)
                throw Error("line {}: metadata after the header", line);
            current.remove_prefix(1);
            const size_t colon = current.find(':');
            if (colon == std::string_view::npos)
                throw Error("line {}: metadata is not '# key: value'", line);
            auto trim = [](std::string_view value) {
                while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
                while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
                return std::string(value);
            };
            table.meta.emplace_back(trim(current.substr(0, colon)), trim(current.substr(colon + 1)));
            continue;
        }
        if (!header) {
            if (current == LegacyHeader)
                legacy = true;
            else if (current != Header)
                throw Error("line {}: expected the header '{}'", line, Header);
            header = true;
            continue;
        }

        std::vector<std::string_view> fields;
        for (size_t start = 0;;) {
            const size_t tab = current.find('\t', start);
            fields.push_back(current.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
            if (tab == std::string_view::npos)
                break;
            start = tab + 1;
        }
        if (legacy) {
            // work/executor/mode → bench/implementation/params: `ConstrainGpu` (work
            // Constrain, executor gpu) is the gpu variant of Constrain; mode offload became
            // the work ConstrainOffload; a cpu executor was one thread.
            if (fields.size() != LegacyColumns)
                throw Error("line {}: {} columns, expected {}", line, fields.size(), LegacyColumns);
            const std::string_view work = fields[2], executor = fields[3], mode = fields[4];
            std::string bench(work != "-" ? work : fields[1]);
            if (mode == "offload")
                bench += "Offload";
            const std::string params = executor == "cpu" ? "workers=1" : "-";
            migrated = std::vector<std::string>{std::string(fields[0]), bench, std::string(executor), params};
            fields.erase(fields.begin() + 1, fields.begin() + 5);
            fields.insert(fields.begin(), 3, std::string_view{});
            for (size_t i = 0; i < 4; ++i)
                fields[i] = migrated[i];
        }
        if (fields.size() != Columns)
            throw Error("line {}: {} columns, expected {}", line, fields.size(), Columns);

        ResultRow row{
            .group = std::string(fields[0]), .bench = std::string(fields[1]),
            .implementation = std::string(fields[2]), .params = std::string(fields[3]), .n = std::string(fields[4]),
            .stage = std::string(fields[5]), .metric = std::string(fields[6]),
            .value = parseNumber(fields[7], line), .measured = std::string(fields[9]),
        };
        if (fields[8] != "-") {
            for (size_t start = 0;;) {
                const size_t comma = fields[8].find(',', start);
                row.samples.push_back(parseNumber(
                    fields[8].substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start), line));
                if (comma == std::string_view::npos)
                    break;
                start = comma + 1;
            }
        }
        table.rows.push_back(std::move(row));
    }
    if (!header)
        throw Error("no header: not a results table");
    return table;
}

}
