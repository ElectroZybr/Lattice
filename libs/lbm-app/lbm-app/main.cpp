// lattice-benchmarks (lbm): run benchmark suites, keep their history in a git repository,
// compare runs and find regressions. See docs/ru/articles/BmHistory.md.
#include <cctype>
#include <charconv>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <lattice-tools/Charconv.hpp>
#include <lbm-history/Git.hpp>
#include <lbm-history/Machine.hpp>
#include <lbm-history/Process.hpp>
#include <lbm-history/Repository.hpp>
#include <lbm-history/Statistics.hpp>
#include <lbm-runner/Output.hpp>
#include <lbm-runner/Results.hpp>
#include <lattice-tools/Exception.hpp>
#include <lattice-logger/Logger.hpp>

#include "Config.hpp"

namespace {

namespace fs = std::filesystem;
namespace Bh = Lattice::BmHistory;
namespace Bmr = Lattice::Benchmarks;
struct LbmTool;
using Error = Lattice::Exception<LbmTool>;

constexpr std::string_view Usage = R"(lattice-benchmarks — usage: lbm <command> [arguments]

  list [suite]                               suites (xmake targets <Name>.bench or lbm.toml), or the benches of one
  run <suite> [filter] [--save] [-m text] [--tag name] [--details]   run, show against the baseline;
                                             in a terminal asks to save (--save/--no-save: don't ask)
  log [--suite s] [--last k] [--all-machines]   saved runs, newest first: run, date, suite, code, tags
  show [rev] [filter] [--suite s] [--metric m] [--details]   one saved run (default: the last)
  compare [A] [B] [--baseline tag] [--changed] [--details]   A → B (default: baseline, else the run before, → the last)
  history <filter> [--suite s] [--metric m] [--n N] [--last k]   a row through the history, shifts marked
  regressions [--since rev] [--suite s]      shifts to the worse; exit code 1 if any
  tag [name [rev]] | tag -d <name>          baselines: list, set (default: the last run), delete

  run output: default progress only; -v text + progress; -vv legacy text
  filter: bench, group or mask (Walls/*Gpu); metric: mask (default Time.median)
  xmake suites are built before list/run unless --no-build
  rev: commit, run id (r0007), tag, HEAD~1
  --machine <id> reads another machine's results; --results <dir> another repository
)";

// ---------- arguments ----------

struct Arguments {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    std::set<std::string> flags;
    unsigned verbosity = 0;

    bool has(std::string_view flag) const { return flags.contains(std::string(flag)); }
    std::string option(std::string_view name, std::string fallback = {}) const {
        const auto it = options.find(std::string(name));
        return it == options.end() ? fallback : it->second;
    }
    std::string at(size_t index, std::string fallback = {}) const {
        return index < positional.size() ? positional[index] : fallback;
    }
};

Arguments parse(int argc, char** argv, int from) {
    static const std::set<std::string> valued{"-m", "--message", "--suite", "--machine", "--baseline", "--metric",
                                              "--n", "--last", "--since", "--results", "--tag", "-d"};
    Arguments arguments;
    for (int i = from; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-v" || argument == "--verbose") {
            arguments.verbosity = std::min(2u, arguments.verbosity + 1);
        } else if (argument == "-vv") {
            arguments.verbosity = 2;
        } else if (valued.contains(argument)) {
            if (i + 1 >= argc)
                throw Error("{} needs a value", argument);
            arguments.options[argument == "-m" ? "--message" : argument == "-d" ? "--delete" : argument] = argv[++i];
        } else if (argument.starts_with("--")) {
            arguments.flags.insert(argument);
        } else {
            arguments.positional.push_back(argument);
        }
    }
    return arguments;
}

// ---------- helpers ----------

bool glob(std::string_view mask, std::string_view text) {
    if (mask.empty())
        return text.empty();
    if (mask.front() == '*')
        return glob(mask.substr(1), text) || (!text.empty() && glob(mask, text.substr(1)));
    return !text.empty() && (mask.front() == '?' || mask.front() == text.front()) && glob(mask.substr(1), text.substr(1));
}

/// Name of a row's variant (`cpu×16`, named by BmRunner); empty for a work with one
/// implementation.
std::string variantOf(const Bmr::ResultRow& row) {
    return row.variant == "-" ? std::string{} : row.variant;
}

/// A bench filter as BmRunner understands it: group, work or mask over
/// group/work[/variant].
bool benchMatches(const Bmr::ResultRow& row, std::string_view filter) {
    if (filter.empty() || filter == row.group || filter == row.bench)
        return true;
    const std::string work = row.group + "/" + row.bench;
    const std::string variant = variantOf(row);
    return glob(filter, work) || (!variant.empty() && glob(filter, work + "/" + variant));
}

Bmr::Unit unitOf(const Bmr::ResultRow& row) {
    return Bmr::parseUnit(row.unit);
}

std::string value(const Bmr::ResultRow& row, double number) {
    if (!std::isfinite(number))
        return std::format("{}", number);
    return Bmr::Output::formatValue(number, unitOf(row));
}

/// `group/work[/variant]`.
std::string rowName(const Bmr::ResultRow& row) {
    const std::string variant = variantOf(row);
    return row.group + "/" + row.bench + (variant.empty() ? "" : "/" + variant);
}

/// Text without terminal colour codes.
std::string plain(std::string_view text) {
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && !std::isalpha(static_cast<unsigned char>(text[i])))
                ++i;
            continue;
        }
        result += text[i];
    }
    return result;
}

/// A suite: its binary and, when it comes from xmake, the target that builds it.
struct Suite {
    std::string name;
    fs::path binary;
    std::string target;
};

/// Suites of the xmake project in `folder`: targets `<Name>.bench`, named `<Name>`.
std::map<std::string, Suite> discover(const fs::path& folder) {
    std::map<std::string, Suite> suites;
    if (!fs::exists(folder / "xmake.lua"))
        return suites;
    const auto listed = Bh::runProcess("xmake", {"show", "-l", "targets"}, folder);
    if (listed.status != 0)
        return suites;
    std::string token;
    std::vector<std::string> targets;
    for (const char c : plain(listed.out) + "\n") {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (token.ends_with(".bench"))
                targets.push_back(token);
            token.clear();
        } else {
            token += c;
        }
    }
    for (const auto& target : targets) {
        const auto shown = Bh::runProcess("xmake", {"show", "-t", target}, folder);
        const std::string text = plain(shown.out);
        const size_t at = text.find("targetfile:");
        if (shown.status != 0 || at == std::string::npos)
            continue;
        std::string file = text.substr(at + 11, text.find('\n', at) - at - 11);
        while (!file.empty() && std::isspace(static_cast<unsigned char>(file.front()))) file.erase(file.begin());
        while (!file.empty() && std::isspace(static_cast<unsigned char>(file.back()))) file.pop_back();
        const std::string name = target.substr(0, target.size() - 6);
        suites[name] = {.name = name, .binary = folder / file, .target = target};
    }
    return suites;
}

struct Context {
    Lbm::Config config;
    std::string machine;
    mutable std::optional<std::map<std::string, Suite>> discovered;

    /// lbm.toml's suites, else the xmake project's `*.bench` targets.
    const std::map<std::string, Suite>& known() const {
        if (!discovered) {
            discovered.emplace();
            for (const auto& [name, binary] : config.suites)
                (*discovered)[name] = {.name = name, .binary = binary};
            if (discovered->empty())
                *discovered = discover(config.folder);
        }
        return *discovered;
    }

    Bh::Repository repository() const { return Bh::Repository::open(config.results); }
    bool hasRepository() const { return fs::exists(config.results / ".git"); }
    Bh::Thresholds thresholds() const {
        return {.relative = config.relative, .alpha = config.alpha, .noisy = config.noisy};
    }

    /// A suite by name (lbm.toml or xmake), or a path to a binary.
    Suite suite(std::string_view name) const {
        const auto& suites = known();
        if (const auto it = suites.find(std::string(name)); it != suites.end())
            return it->second;
        if (name.ends_with(".bench"))
            if (const auto it = suites.find(std::string(name.substr(0, name.size() - 6))); it != suites.end())
                return it->second;
        const fs::path path(name);
        if (fs::exists(path))
            return {.name = path.filename().string(), .binary = fs::absolute(path)};
        std::string names;
        for (const auto& [suite, info] : suites)
            names += (names.empty() ? "" : ", ") + suite;
        throw Error("unknown suite '{}'; known: [{}]", name, names);
    }

    /// Build an xmake suite so the run measures the current code.
    void build(const Suite& suite) const {
        if (suite.target.empty())
            return;
        Logger::message(TextFormatter::format("<mut>building {}…</>", suite.target));
        const auto built = Bh::runProcess("xmake", {"build", "-y", suite.target}, config.folder);
        if (built.status != 0)
            throw Error("xmake build {} failed:\n{}{}", suite.target, plain(built.out), plain(built.err));
    }

    /// Suites to look at: --suite, else every table of this machine in the repository.
    std::vector<std::string> suites(const Arguments& arguments) const {
        if (const auto suite = arguments.option("--suite"); !suite.empty())
            return {suite};
        std::vector<std::string> result;
        const auto folder = config.results / machine;
        if (fs::is_directory(folder))
            for (const auto& entry : fs::directory_iterator(folder))
                if (entry.path().extension() == ".tsv")
                    result.push_back(entry.path().stem().string());
        std::ranges::sort(result);
        if (result.empty())
            throw Error("no saved results of machine '{}' in {}", machine, config.results.string());
        return result;
    }
};

/// Local package repositories of the xmake project (`xmake repo -l`): the code of the
/// packages a suite is built from (the Lattice kernel for LatticeLab).
std::vector<fs::path> packageRepositories(const fs::path& folder) {
    std::vector<fs::path> paths;
    if (!fs::exists(folder / "xmake.lua"))
        return paths;
    const auto listed = Bh::runProcess("xmake", {"repo", "-l"}, folder);
    if (listed.status != 0)
        return paths;
    const std::string text = plain(listed.out);
    bool local = false;
    size_t at = 0;
    while (at < text.size()) {
        const size_t end = text.find('\n', at);
        std::string_view line(text.data() + at, (end == std::string::npos ? text.size() : end) - at);
        at = end == std::string::npos ? text.size() : end + 1;
        if (line.find("repositories:") != std::string_view::npos) {
            local = line.find("local") != std::string_view::npos;
            continue;
        }
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1);
        const size_t space = line.find(' ');
        if (!local || space == std::string_view::npos)
            continue;
        std::string_view path = line.substr(space + 1);
        path = path.substr(0, path.find(' '));
        if (fs::is_directory(path))
            paths.emplace_back(path);
    }
    return paths;
}

std::string codeTrailer(const fs::path& path) {
    const auto state = Bh::codeState(path);
    if (!state)
        return {};
    return std::format("{}@{}{}", state->name, state->commit, state->dirty ? "+dirty" : "");
}

/// Metrics shown by default: Time.median stands for the Time summaries (min, mean,
/// perOperation and the raw `time` series are statistics of the same samples); fit
/// errors are not shown.
bool shownByDefault(std::string_view metric) {
    if (metric.ends_with(".error") || metric == "error")
        return false;   // quality of a fit, not speed
    return !(metric.starts_with("Time.") && metric != "Time.median" && metric.find('(') == std::string_view::npos);
}

// ---------- results view ----------

/// Default baseline tag: `run` and `compare` measure against it when it exists.
constexpr std::string_view BaselineTag = "baseline";

/// A table cell: plain text and a theme style (ok, wrn, err, mut, …) or none; a time
/// keeps its unit apart, coloured by magnitude and aligned as its own column.
struct Cell {
    std::string text;
    std::string_view style;
    std::string unit;
};

/// Colour of a time unit, as the old runner: ns grey, us cyan, ms violet, s yellow.
std::string_view unitStyle(std::string_view unit) {
    if (unit == "ns") return "mut";
    if (unit == "us") return "a2";
    if (unit == "ms") return "a";
    return "wrn";
}

/// A time cell: the number in `style`, the unit coloured.
Cell timeCell(double nanoseconds, std::string_view style) {
    const std::string text = Bmr::Output::formatValue(nanoseconds, Bmr::Unit::Nanoseconds);
    const size_t space = text.rfind(' ');
    if (space == std::string::npos)
        return {text, style};
    return {text.substr(0, space), style, text.substr(space + 1)};
}

/// Width in terminal columns: UTF-8 code points.
size_t columns(std::string_view text) {
    size_t width = 0;
    for (const unsigned char c : text)
        width += (c & 0xC0) != 0x80;
    return width;
}

/// Columns aligned by visible width; `right` columns are numbers. Units of time cells
/// form a column of two characters after the number.
void printTable(const std::vector<std::string>& header, const std::vector<std::vector<Cell>>& rows,
                const std::set<size_t>& right) {
    std::vector<size_t> widths(header.size()), units(header.size());
    for (const auto& row : rows)
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i) {
            widths[i] = std::max(widths[i], columns(row[i].text));
            units[i] = std::max(units[i], row[i].unit.empty() ? size_t(0) : 1 + std::max<size_t>(2, columns(row[i].unit)));
        }
    for (size_t i = 0; i < header.size(); ++i)
        widths[i] = std::max(widths[i], columns(header[i]) > units[i] ? columns(header[i]) - units[i] : 0);

    auto fill = [](size_t count) { return std::string(count, ' '); };
    std::string line = " ";
    for (size_t i = 0; i < header.size(); ++i) {
        const size_t total = widths[i] + units[i], width = columns(header[i]);
        line += (right.contains(i) ? fill(total - width) + header[i] : header[i] + fill(total - width)) + "  ";
    }
    Logger::message(TextFormatter::format("<light><b>{}<//>", line));
    for (const auto& row : rows) {
        std::string markup = " ";
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i) {
            const Cell& cell = row[i];
            const std::string gap = fill(widths[i] - columns(cell.text));
            std::string text = cell.style.empty() ? cell.text : std::format("<{}>{}</>", cell.style, cell.text);
            text = right.contains(i) ? gap + text : text + gap;
            if (units[i]) {
                const std::string unit = cell.unit.empty() ? std::string{} : " " + cell.unit;
                text += cell.unit.empty() ? fill(units[i])
                                          : std::format("<{}>{}</>", unitStyle(cell.unit), unit) + fill(units[i] - columns(unit));
            }
            markup += text + "  ";
        }
        Logger::message(TextFormatter::format("{}", markup));
    }
}

/// 1000 → 1K, 1000000 → 1M, 1500 → 1.5K.
std::string compactN(std::string_view n) {
    double value = 0;
    if (Lattice::fromChars(n.data(), n.data() + n.size(), value).ec != std::errc{})
        return std::string(n);
    for (const auto& [scale, suffix] : {std::pair{1e9, "G"}, {1e6, "M"}, {1e3, "K"}})
        if (value >= scale)
            return std::format("{:.3g}{}", value / scale, suffix);
    return std::string(n);
}

std::string_view variationStyle(double cv) {
    return cv < 0.02 ? "ok" : cv < 0.05 ? "wrn" : "err";
}

/// What a run is measured against.
struct Reference {
    std::optional<Bmr::ResultTable> table;
    std::string label;   // "baseline (r0002)", "r0003"
};

std::string runId(const Bh::Repository& repository, const std::string& commit) {
    for (const auto& run : repository.runs())
        if (run.commit == commit)
            return run.trailer("Run");
    return commit.substr(0, 10);
}

/// The baseline tag when it exists, else the last saved run of the suite; says which.
Reference reference(const Context& context, std::string_view suite, std::string_view tag, bool quiet = false) {
    if (!context.hasRepository()) {
        if (!quiet)
            Logger::message(TextFormatter::format("<mut>no saved runs yet: nothing to compare with</>"));
        return {};
    }
    const auto repository = context.repository();
    for (const auto& [name, commit] : repository.baselines())
        if (name == tag)
            if (auto table = repository.load(commit, context.machine, suite))
                return {std::move(table), std::format("{} ({})", name, runId(repository, commit))};
    const auto runs = repository.runs(context.machine, suite);
    if (runs.empty()) {
        if (!quiet)
            Logger::message(TextFormatter::format("<mut>no saved runs of {} yet: nothing to compare with</>", suite));
        return {};
    }
    if (!quiet)
        Logger::message(TextFormatter::format("<wrn>baseline tag '{}' not found</> <mut>— comparing with the last run {}</>",
            tag, runs.front().trailer("Run")));
    return {repository.load(runs.front().commit, context.machine, suite), runs.front().trailer("Run")};
}

struct Tally {
    size_t faster = 0, slower = 0, same = 0, noisy = 0, fresh = 0;
};

/// A row's change against the reference: coloured Δ, its mark and the tally it adds to.
struct Change {
    bool found = false;
    double reference = 0;
    Cell delta, mark;
    std::string_view style = "light";   // colour of the time itself in the side-by-side view
    bool changed = false;
};

Change changeOf(const Bmr::ResultRow& row, const std::map<std::string, const Bmr::ResultRow*>& before,
                const Bh::Thresholds& thresholds, Tally& tally) {
    Change change;
    const auto it = before.find(Bmr::rowKey(row));
    if (it == before.end()) {
        ++tally.fresh;
        change.delta = {"new", "mut"};
        return change;
    }
    const auto& old = *it->second;
    const auto shift = Bh::shift(old.value, old.samples, row.value, row.samples, thresholds);
    change.found = true;
    change.reference = old.value;
    change.delta = {std::format("{:+.1f}%", (shift.ratio - 1) * 100), "mut"};
    if (shift.significant && shift.ratio > 1) {
        change.delta.style = change.style = "err";
        change.mark = {"▼", "err"};
        change.changed = true;
        ++tally.slower;
    } else if (shift.significant) {
        change.delta.style = change.style = "ok";
        change.mark = {"▲", "ok"};
        change.changed = true;
        ++tally.faster;
    } else if (shift.noisy) {
        change.mark = {"~", "mut"};
        ++tally.noisy;
    } else {
        ++tally.same;
    }
    return change;
}

/// Works with several variants side by side: a row per N, a column per variant (time
/// coloured by its change against the reference), the best variant, the thresholds.
void sideBySide(const std::string& work, const std::vector<const Bmr::ResultRow*>& rows,
                const std::vector<const Bmr::ResultRow*>& comparison,
                const std::map<std::string, const Bmr::ResultRow*>& before, const Bh::Thresholds& thresholds,
                bool changedOnly, Tally& tally) {
    std::vector<std::string> variants;
    std::vector<std::string> sizes;
    std::map<std::pair<std::string, std::string>, const Bmr::ResultRow*> cell;   // (n, variant) → row
    for (const auto* row : rows) {
        const std::string variant = variantOf(*row);
        if (std::ranges::find(variants, variant) == variants.end())
            variants.push_back(variant);
        if (std::ranges::find(sizes, row->n) == sizes.end())
            sizes.push_back(row->n);
        cell[{row->n, variant}] = row;
    }
    std::ranges::sort(sizes, {}, [](const std::string& n) { return std::strtod(n.c_str(), nullptr); });

    std::vector<std::string> header{work};
    header.insert(header.end(), variants.begin(), variants.end());
    header.push_back("best");
    std::vector<std::vector<Cell>> lines;
    for (const auto& n : sizes) {
        std::vector<Cell> line{{"  " + compactN(n), "a2"}};
        const Bmr::ResultRow* best = nullptr;
        bool changed = false;
        for (const auto& variant : variants) {
            const auto it = cell.find({n, variant});
            if (it == cell.end()) {
                line.push_back({"-", "mut"});
                continue;
            }
            const auto& row = *it->second;
            const Change change = changeOf(row, before, thresholds, tally);
            changed |= change.changed;
            line.push_back(timeCell(row.value, change.style));
            if (!best || row.value < best->value)
                best = &row;
        }
        line.push_back({best ? variantOf(*best) : "-", "a2"});
        if (!changedOnly || changed)
            lines.push_back(std::move(line));
    }
    if (lines.empty())
        return;
    std::set<size_t> right;
    for (size_t i = 1; i <= variants.size(); ++i)
        right.insert(i);
    printTable(header, lines, right);

    std::string crossovers;
    for (const auto* row : comparison)
        if (row->metric.starts_with("crossover:")) {
            const std::string slower = row->metric.substr(10);
            crossovers += row->value > 0
                ? std::format("  <light>{}</> <mut>beats</> <light>{}</> <mut>from N ≈</> <light>{}</>", variantOf(*row),
                      slower, compactN(std::format("{:.0f}", row->value)))
                : std::format("  <light>{}</> <mut>beats</> <light>{}</> <mut>at every N</>", variantOf(*row), slower);
        }
    if (!crossovers.empty())
        Logger::message(TextFormatter::format("{}", crossovers));
}

/// Time per bench and N against a reference, the change coloured by significance.
/// Works with one implementation: a row per point; with several: side by side.
/// Returns the tally.
Tally resultsTable(const Bmr::ResultTable& table, const Reference& against, const Bh::Thresholds& thresholds,
                   bool changedOnly, bool measured = false) {
    std::map<std::string, const Bmr::ResultRow*> before;
    if (against.table)
        for (const auto& row : against.table->rows)
            before.emplace(Bmr::rowKey(row), &row);

    std::vector<Bmr::ResultRow> rows;
    for (const auto& row : table.rows)
        if (row.metric == "Time.median" && row.n != "-")
            rows.push_back(row);
    Bmr::sortRows(rows);

    // Works and how many variants each has.
    std::map<std::string, std::set<std::string>> variants;
    for (const auto& row : rows)
        variants[row.group + "/" + row.bench].insert(variantOf(row));
    std::map<std::string, std::vector<const Bmr::ResultRow*>> comparisons;
    for (const auto& row : table.rows)
        if (row.stage == "compare")
            comparisons[row.group + "/" + row.bench].push_back(&row);

    std::vector<std::string> header{"bench", "time", "per N", "cv"};
    if (measured)
        header.push_back("measured");
    if (against.table)
        header.insert(header.end(), {against.label, "Δ", ""});
    std::vector<std::vector<Cell>> cells;
    std::map<std::string, std::vector<const Bmr::ResultRow*>> grouped;   // works with several variants
    Tally tally;
    for (const auto& row : rows) {
        const std::string work = row.group + "/" + row.bench;
        if (variants[work].size() > 1) {
            grouped[work].push_back(&row);
            continue;
        }
        const double cv = Bh::variation(row.samples);
        const double n = std::strtod(row.n.c_str(), nullptr);
        std::vector<Cell> line{
            {rowName(row) + "/" + row.n, "light"}, timeCell(row.value, "light"),
            n > 0 ? timeCell(row.value / n, "") : Cell{"-", "mut"},
            {row.samples.size() >= 2 ? std::format("{:.1f}%", cv * 100) : "-", variationStyle(cv)},
        };
        if (measured)
            line.push_back({row.measured, "a2"});
        bool changed = false;
        if (against.table) {
            Change change = changeOf(row, before, thresholds, tally);
            changed = change.changed;
            line.push_back(change.found ? timeCell(change.reference, "mut") : Cell{"-", "mut"});
            line.push_back(std::move(change.delta));
            line.push_back(std::move(change.mark));
        }
        if (changedOnly && !changed)
            continue;
        cells.push_back(std::move(line));
    }
    if (!cells.empty())
        printTable(header, cells, {1, 2, 3, 4, 5});
    for (const auto& [work, workRows] : grouped) {
        if (!cells.empty() || &work != &grouped.begin()->first)
            Logger::blank();
        sideBySide(work, workRows, comparisons[work], before, thresholds, changedOnly, tally);
    }
    if (cells.empty() && grouped.empty())
        Logger::message(TextFormatter::format("<mut>{}</>", changedOnly ? "no significant changes" : "no timings"));

    if (against.table) {
        std::string summary = std::format("<ok>{} faster</> <mut>·</> <{}>{} slower</> <mut>· {} unchanged",
            tally.faster, tally.slower ? "err" : "mut", tally.slower, tally.same);
        if (tally.noisy)
            summary += std::format(" · {} noisy", tally.noisy);
        if (tally.fresh)
            summary += std::format(" · {} new", tally.fresh);
        Logger::message(TextFormatter::format("{}</>", summary));
    }
    return tally;
}

/// --details: phase shares at the largest N of each traced bench, and the executor
/// comparisons (fixed + per·N, crossover).
void details(const Bmr::ResultTable& table) {
    std::map<std::string, std::pair<double, std::vector<const Bmr::ResultRow*>>> phases;   // bench → (N, rows)
    for (const auto& row : table.rows) {
        if (!row.metric.starts_with("Phases.") || row.n == "-" || row.metric.ends_with(".rate"))
            continue;
        const double n = std::strtod(row.n.c_str(), nullptr);
        auto& [largest, rows] = phases[rowName(row)];   // per variant: cpu×16 and gpu apart
        if (n > largest) {
            largest = n;
            rows.clear();
        }
        if (n == largest)
            rows.push_back(&row);
    }
    for (const auto& [bench, entry] : phases) {
        double wall = 0;
        for (const auto* row : entry.second)
            if (row->metric == "Phases.wall")
                wall = row->value;
        if (wall <= 0)
            continue;
        // Host phases share the wall; device time runs inside them (in Complete), so it
        // is a separate share, not a part of the sum.
        std::vector<std::pair<double, std::string>> host, device;
        for (const auto* row : entry.second) {
            if (row->metric == "Phases.wall" || row->value <= 0)
                continue;
            std::string name = row->metric.substr(7);
            const bool onDevice = name.ends_with("@dev");
            if (onDevice)
                name.resize(name.size() - 4);
            if (name.starts_with("Execute:"))
                name = name.substr(8);
            (onDevice ? device : host).emplace_back(row->value / wall, std::move(name));
        }
        std::erase_if(host, [](const auto& share) { return share.first < 0.01; });
        if (host.size() <= 1 && device.empty())
            continue;   // one host phase: nothing to split
        std::ranges::sort(host, std::greater{});
        std::string text;
        for (const auto& [share, name] : host)
            text += std::format("{}{} {:.0f}%", text.empty() ? "" : " │ ", name, share * 100);
        std::string onDevice;
        for (const auto& [share, name] : device)
            onDevice += std::format("  <a2>on device</> <light>{}</> <mut>{:.0f}%</>", name, share * 100);
        Logger::message(TextFormatter::format(" <light>{}</> <a2>@{}</> <mut>{}</>{}", bench,
            compactN(std::format("{}", entry.first)), text, onDevice));
    }

    std::map<std::string, std::vector<const Bmr::ResultRow*>> works;
    for (const auto& row : table.rows)
        if (row.stage == "compare")
            works[row.group + "/" + row.bench].push_back(&row);
    for (const auto& [work, rows] : works) {
        std::map<std::string, std::pair<double, double>> fits;
        std::string crossover;
        for (const auto* row : rows) {
            if (row->metric == "fixed") fits[variantOf(*row)].first = row->value;
            if (row->metric == "per") fits[variantOf(*row)].second = row->value;
            if (row->metric.starts_with("crossover:"))
                crossover += row->value > 0
                    ? std::format(" <light>{}</> <mut>faster than {} from N ≈</> <light>{}</>", variantOf(*row),
                          row->metric.substr(10), compactN(std::format("{:.0f}", row->value)))
                    : std::format(" <light>{}</> <mut>faster than {} at every N</>", variantOf(*row), row->metric.substr(10));
        }
        std::string text;
        for (const auto& [executor, fit] : fits)
            text += std::format("{}<a2>{}</> {}{}·N", text.empty() ? "" : " <mut>·</> ", executor,
                fit.first > 0 ? Bmr::Output::formatValue(fit.first, Bmr::Unit::Nanoseconds) + " + " : "",
                Bmr::Output::formatValue(fit.second, Bmr::Unit::Nanoseconds));
        Logger::message(TextFormatter::format(" <light>{}</>  {}  <mut>→</>{}", work, text, crossover));
    }
}

std::string ask(std::string_view question) {
    LogSystem::writeConsole(TextFormatter::format("<light>{}</> ", question).render());
    std::string answer;
    std::getline(std::cin, answer);
    return answer;
}

/// The row's name in tables: `group/work[/variant]/N`.
std::string rowLabel(const Bmr::ResultRow& row) {
    return rowName(row) + (row.n == "-" ? "" : "/" + row.n);
}

/// A metric's value as a cell: times split into number and coloured unit.
Cell valueCell(const Bmr::ResultRow& row, double number, std::string_view style) {
    if (std::isfinite(number) && unitOf(row) == Bmr::Unit::Nanoseconds)
        return timeCell(number, style);
    return {value(row, number), style};
}

Cell spreadCell(std::span<const double> samples) {
    if (samples.size() < 2)
        return {"-", "mut"};
    const double cv = Bh::variation(samples);
    return {std::format("{:.1f}%", cv * 100), variationStyle(cv)};
}

/// Code of a run: bright where it differs from `older` (dim when there is nothing to differ from).
std::vector<Cell> codeCells(const Bh::RunInfo& run, const std::vector<std::string>* older) {
    std::string text;
    bool changed = false;
    for (const auto& value : run.all("Code")) {
        text += (text.empty() ? "" : "  ") + value;
        changed |= older && std::ranges::find(*older, value) == older->end();
    }
    return {{text, changed ? "light" : "mut"}};
}

/// Changes as a table; only significant ones unless `all`.
size_t printChanges(const std::vector<Bh::Change>& changes, bool all, std::string_view metricMask = {}) {
    std::vector<std::vector<Cell>> rows;
    size_t regressions = 0;
    for (const auto& change : changes) {
        if (metricMask.empty() ? !(all || shownByDefault(change.after.metric)) : !glob(metricMask, change.after.metric))
            continue;
        if (!all && !change.shift.significant)
            continue;
        regressions += change.regression;
        const auto& s = change.shift;
        const std::string_view style = change.regression ? "err" : change.improvement ? "ok" : "mut";
        rows.push_back({
            {rowLabel(change.after), "light"}, {change.after.metric, "mut"},
            valueCell(change.before, change.before.value, "mut"), valueCell(change.after, change.after.value, "light"),
            {std::format("{:+.1f}%", (s.ratio - 1) * 100), style},
            {change.regression ? "▼" : change.improvement ? "▲" : s.noisy ? "~" : "", style},
            {s.low == s.high ? "-" : std::format("{:+.1f}..{:+.1f}%", (s.low - 1) * 100, (s.high - 1) * 100), "mut"},
            {s.p >= 1 ? "-" : std::format("{:.3g}", s.p), "mut"},
        });
    }
    if (rows.empty())
        Logger::message(TextFormatter::format("<mut>no significant changes</>"));
    else
        printTable({"bench", "metric", "before", "after", "Δ", "", "95%", "p"}, rows, {2, 3, 4, 6, 7});
    return regressions;
}

// ---------- commands ----------

int list(const Context& context, const Arguments& arguments) {
    if (arguments.positional.empty()) {
        if (context.known().empty()) {
            Logger::message(TextFormatter::format(
                "<wrn>no suites: no xmake targets '<Name>.bench' and no lbm.toml in {}</>", context.config.folder.string()));
            return 1;
        }
        std::vector<std::vector<Cell>> rows;
        for (const auto& [name, suite] : context.known()) {
            const bool built = fs::exists(suite.binary);
            rows.push_back({{name, "light"}, {suite.target.empty() ? "lbm.toml" : "xmake " + suite.target, "mut"},
                {fs::relative(suite.binary, context.config.folder).string(), "mut"},
                {built ? "" : "not built", "wrn"}});
        }
        printTable({"suite", "source", "binary", ""}, rows, {});
        return 0;
    }
    const auto suite = context.suite(arguments.at(0));
    if (!arguments.has("--no-build"))
        context.build(suite);
    return Bh::runAttached(suite.binary, {"--list"});
}

int run(const Context& context, const Arguments& arguments) {
    if (arguments.positional.empty())
        throw Error("run needs a suite");
    if (!arguments.option("--tag").empty() && !arguments.has("--save"))
        throw Error("--tag needs --save");
    const auto suite = context.suite(arguments.at(0));
    const std::string& name = suite.name;
    const fs::path& binary = suite.binary;
    if (!arguments.has("--no-build"))
        context.build(suite);
    const std::string filter = arguments.at(1);
    const fs::path tsv = fs::temp_directory_path() / std::format("lbm-{}-{:08x}.tsv", name, std::random_device{}());

    std::vector<std::string> suiteArguments;
    if (!filter.empty())
        suiteArguments.push_back(filter);
    if (arguments.verbosity) suiteArguments.push_back(arguments.verbosity == 1 ? "-v" : "-vv");
    suiteArguments.insert(suiteArguments.end(), {"--tsv", tsv.string()});
    if (const int status = Bh::runAttached(binary, suiteArguments); status != 0)
        throw Error("suite '{}' failed with status {}", name, status);

    std::ifstream in(tsv, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    in.close();
    fs::remove(tsv);
    auto fresh = Bmr::parseTsv(text);

    // Results against the baseline tag, else the last saved run.
    Logger::blank();
    Logger::message(TextFormatter::format("<a2><b>{}<//> <mut>{}</>", name, filter.empty() ? "all benches" : filter));
    const auto against = reference(context, name, arguments.option("--baseline", std::string(BaselineTag)));
    resultsTable(fresh, against, context.thresholds(), false);
    if (arguments.has("--details"))
        details(fresh);

    // Save: --save, or a question in a terminal — "as baseline" while there is none.
    bool save = arguments.has("--save");
    std::string message = arguments.option("--message");
    std::string tag = arguments.option("--tag");
    if (!save && Bh::interactiveTerminal() && !arguments.has("--no-save")) {
        bool hasBaseline = false;
        if (context.hasRepository())
            for (const auto& [known, commit] : context.repository().baselines())
                hasBaseline |= known == BaselineTag;
        Logger::blank();
        const std::string answer = ask(hasBaseline ? "Save this run? [y/N]" : "Save this run as baseline? [y/N]");
        save = answer == "y" || answer == "Y" || answer == "yes";
        if (save) {
            message = ask("Message (what changed; Enter to skip):");
            if (!hasBaseline)
                tag = std::string(BaselineTag);
        }
    }
    if (!save)
        return 0;

    auto repository = context.repository();
    Bh::SaveRequest request{.machine = context.machine, .machineDescription = Bh::currentMachine().description,
                            .suite = name, .table = fresh, .message = message};
    // The suite's repository, the local package repositories of its xmake project and
    // lbm.toml's `code`: each once.
    std::vector<fs::path> sources{binary};
    if (!suite.target.empty())
        for (auto& path : packageRepositories(context.config.folder))
            sources.push_back(std::move(path));
    sources.insert(sources.end(), context.config.code.begin(), context.config.code.end());
    std::set<std::string> recorded;
    for (const auto& path : sources)
        if (const auto code = codeTrailer(path); !code.empty() && recorded.insert(code).second)
            request.trailers.emplace_back("Code", code);
    if (const auto* build = fresh.find("build"))
        request.trailers.emplace_back("Build", *build);
    if (const auto* used = fresh.find("filter"))
        request.trailers.emplace_back("Filter", *used);
    const auto saved = repository.save(request);
    std::string note;
    if (!tag.empty()) {
        repository.setBaseline(tag, saved.commit);
        note = std::format(" <mut>as</> <light>{}</>", tag);
    }
    Logger::message(TextFormatter::format("<ok>saved</> <light>{}</>{} <mut>{}</>", saved.trailer("Run"), note,
        saved.commit.substr(0, 10)));
    return 0;
}

/// Saved runs, newest first, as a chain like `git log --graph`: a node per run (yellow
/// when tagged — the same glyph keeps the rail straight in any font), its code (what
/// changed against the run before it is bright) and message.
int log(const Context& context, const Arguments& arguments) {
    const auto repository = context.repository();
    const bool everyMachine = arguments.has("--all-machines");
    const auto runs = repository.runs(everyMachine ? std::string_view{} : std::string_view(context.machine),
                                      arguments.option("--suite"));
    if (runs.empty()) {
        Logger::message(TextFormatter::format("<mut>no saved runs{}; save one: lbm run <suite> --save</>",
            everyMachine ? "" : " of machine " + context.machine));
        return 0;
    }
    std::map<std::string, std::vector<std::string>> tags;
    for (const auto& [name, commit] : repository.baselines())
        tags[commit].push_back(name);

    const size_t last = arguments.option("--last").empty() ? runs.size() : std::stoul(arguments.option("--last"));
    const size_t shown = std::min(last, runs.size());
    for (size_t i = 0; i < shown; ++i) {
        const auto& run = runs[i];
        const bool tagged = tags.contains(run.commit);
        std::string date = run.date.substr(0, 16);
        std::ranges::replace(date, 'T', ' ');
        const std::string filter = run.trailer("Filter").empty() || run.trailer("Filter") == "*"
            ? "all benches" : run.trailer("Filter");

        std::string head = std::format("{} <a2><b>{}<//>  <mut>{}</>  <light>{}</> <mut>· {}</>",
            tagged ? "<wrn>●</>" : "<a2>●</>", run.trailer("Run"), date, run.trailer("Suite"), filter);
        if (everyMachine)
            head += std::format(" <mut>· {}</>", run.trailer("Machine"));
        if (tagged) {
            std::string names;
            for (const auto& name : tags[run.commit])
                names += (names.empty() ? "" : ", ") + name;
            head += std::format("  <wrn>[{}]</>", names);
        }
        Logger::message(TextFormatter::format("{}", head));

        // Code: bright where it differs from the run before (the next one in the list).
        const auto code = run.all("Code");
        // The first run has nothing to differ from: dim like unchanged code.
        const bool first = i + 1 == runs.size();
        const auto older = first ? std::vector<std::string>{} : runs[i + 1].all("Code");
        std::string line;
        for (const auto& value : code) {
            const bool same = first || std::ranges::find(older, value) != older.end();
            line += std::format("{}<{}>{}</>", line.empty() ? "" : "  ", same ? "mut" : "light", value);
        }
        const std::string rail = i + 1 < shown ? "<mut>│</>" : " ";
        if (!line.empty())
            Logger::message(TextFormatter::format("{}        {}", rail, line));

        // The message without the run id the commit appends; the generated one is dim.
        std::string message = run.subject;
        if (const auto id = run.trailer("Run"); !id.empty() && message.ends_with(", " + id))
            message.resize(message.size() - id.size() - 2);
        const bool generated = message.starts_with("lbm: ") || message.starts_with("bm: ");
        Logger::message(TextFormatter::format("{}        <{}>{}</>", rail, generated ? "mut" : "light", message));
        if (i + 1 < shown)
            Logger::message(TextFormatter::format("<mut>│</>"));
    }
    if (shown < runs.size())
        Logger::message(TextFormatter::format("<mut>… {} older (--last)</>", runs.size() - shown));
    return 0;
}

/// One saved run: the timing table (with the run each row was measured in), or other
/// metrics with --metric.
int show(const Context& context, const Arguments& arguments) {
    const auto repository = context.repository();
    // `lbm show Walls` — the first argument is a filter when it is not a revision.
    std::string revision = arguments.at(0, "HEAD");
    std::string filter = arguments.at(1);
    if (arguments.positional.size() == 1) {
        try {
            (void)repository.resolve(revision);
        } catch (const Lattice::ExceptionBase&) {
            filter = revision;
            revision = "HEAD";
        }
    }
    const std::string metric = arguments.option("--metric", "Time.median");
    for (const auto& suite : context.suites(arguments)) {
        const auto saved = repository.load(revision, context.machine, suite);
        if (!saved)
            continue;
        Bmr::ResultTable shown;
        for (const auto& row : saved->rows)
            if (benchMatches(row, filter) && glob(metric, row.metric))
                shown.rows.push_back(row);
        const std::string label = revision == "HEAD" || revision.size() == 40 ? runId(repository, repository.resolve(revision)) : revision;
        Logger::blank();
        Logger::message(TextFormatter::format("<a2><b>{}<//> <mut>at {}</>", suite, label));
        if (metric == "Time.median") {
            resultsTable(shown, {}, context.thresholds(), false, true);
        } else {
            std::vector<std::vector<Cell>> rows;
            for (const auto& row : shown.rows)
                rows.push_back({{rowLabel(row), "light"}, {row.metric, "mut"}, valueCell(row, row.value, "light"),
                    spreadCell(row.samples), {row.measured, "a2"}});
            printTable({"bench", "metric", "value", "cv", "measured"}, rows, {2, 3});
        }
        if (arguments.has("--details"))
            details(*saved);
    }
    return 0;
}

/// A → B: by default the baseline tag (else the run before the last) → the last run.
/// --metric compares other metrics as a list of changes; the default is the time table.
int compare(const Context& context, const Arguments& arguments) {
    const auto repository = context.repository();
    size_t slower = 0;
    for (const auto& suite : context.suites(arguments)) {
        const auto runs = repository.runs(context.machine, suite);
        if (runs.empty())
            continue;
        std::string a = arguments.at(0), b = arguments.at(1, runs.front().commit);
        Reference before;
        if (a.empty()) {
            const std::string tag = arguments.option("--baseline", std::string(BaselineTag));
            for (const auto& [name, commit] : repository.baselines())
                if (name == tag && commit != runs.front().commit)
                    before = {repository.load(commit, context.machine, suite), std::format("{} ({})", name, runId(repository, commit))};
            if (!before.table) {
                if (runs.size() < 2) {
                    Logger::message(TextFormatter::format("<wrn>{}: one saved run, nothing to compare with</>", suite));
                    continue;
                }
                Logger::message(TextFormatter::format("<wrn>baseline tag '{}' not found</> <mut>— comparing the last two runs</>", tag));
                before = {repository.load(runs[1].commit, context.machine, suite), runs[1].trailer("Run")};
            }
        } else {
            before = {repository.load(a, context.machine, suite), a};
        }
        const auto after = repository.load(b, context.machine, suite);
        if (!before.table || !after) {
            Logger::message(TextFormatter::format("<wrn>{}: no results at {}</>", suite, !before.table ? a : b));
            continue;
        }
        const std::string afterLabel = b.size() == 40 ? runId(repository, b) : b;
        Logger::blank();
        Logger::message(TextFormatter::format("<a2><b>{}<//> <mut>{} → {}</>", suite, before.label, afterLabel));
        if (const auto metric = arguments.option("--metric"); !metric.empty()) {
            slower += printChanges(Bh::compareTables(*before.table, *after, context.thresholds()),
                                   !arguments.has("--changed"), metric);
            continue;
        }
        slower += resultsTable(*after, before, context.thresholds(), arguments.has("--changed")).slower;
        if (arguments.has("--details"))
            details(*after);
    }
    return slower ? 1 : 0;
}

int history(const Context& context, const Arguments& arguments) {
    const auto repository = context.repository();
    const std::string filter = arguments.at(0);
    const std::string metric = arguments.option("--metric", "Time.median");
    const std::string n = arguments.option("--n");
    const size_t last = arguments.option("--last").empty() ? 0 : std::stoul(arguments.option("--last"));
    for (const auto& suite : context.suites(arguments)) {
        const auto versions = repository.history(context.machine, suite);
        if (versions.empty())
            continue;
        for (const auto& row : versions.back().second.rows) {
            if (!benchMatches(row, filter) || !glob(metric, row.metric) || (!n.empty() && row.n != n))
                continue;
            const auto points = Bh::series(versions, Bmr::rowKey(row));
            const auto shifts = Bh::changePoints(points, context.thresholds());
            Logger::blank();
            Logger::message(TextFormatter::format("<a2><b>{}<//> <mut>{} · {}</>", rowLabel(row), row.metric, suite));
            std::vector<std::vector<Cell>> rows;
            const size_t from = last && points.size() > last ? points.size() - last : 0;
            for (size_t i = from; i < points.size(); ++i) {
                const auto& point = points[i];
                Cell change, mark;
                for (const auto& shift : shifts)
                    if (shift.index == i) {
                        const bool better = Bmr::higherIsBetter(row) == (shift.shift.ratio > 1);
                        change = {std::format("{:+.1f}%", (shift.shift.ratio - 1) * 100), better ? "ok" : "err"};
                        mark = {better ? "▲" : "▼", better ? "ok" : "err"};
                    }
                std::string date = point.run.date.substr(0, 16);
                std::ranges::replace(date, 'T', ' ');
                const auto older = i > 0 ? points[i - 1].run.all("Code") : std::vector<std::string>{};
                auto code = codeCells(point.run, i > 0 ? &older : nullptr);
                std::vector<Cell> line{{point.run.trailer("Run"), "a2"}, {date, "mut"}, valueCell(row, point.value, "light"),
                    spreadCell(point.samples), std::move(change), std::move(mark)};
                line.push_back(std::move(code.front()));
                rows.push_back(std::move(line));
            }
            printTable({"run", "date", "value", "cv", "shift", "", "code"}, rows, {2, 3, 4});
        }
    }
    return 0;
}

int regressions(const Context& context, const Arguments& arguments) {
    const auto repository = context.repository();
    const std::string since = arguments.option("--since");
    const std::string sinceCommit = since.empty() ? std::string{} : repository.resolve(since);
    std::vector<std::vector<Cell>> rows;
    for (const auto& suite : context.suites(arguments)) {
        const auto versions = repository.history(context.machine, suite);
        if (versions.empty())
            continue;
        // Runs after `since`, by position in the history.
        size_t sinceIndex = 0;
        if (!sinceCommit.empty())
            for (size_t i = 0; i < versions.size(); ++i)
                if (versions[i].first.commit == sinceCommit)
                    sinceIndex = i + 1;
        for (const auto& row : versions.back().second.rows) {
            if (row.stage == "analysis" || row.stage == "compare" || row.samples.size() < 2)
                continue;
            const auto points = Bh::series(versions, Bmr::rowKey(row));
            for (const auto& shift : Bh::changePoints(points, context.thresholds())) {
                const bool worse = Bmr::higherIsBetter(row) ? shift.shift.ratio < 1 : shift.shift.ratio > 1;
                const auto& run = points[shift.index].run;
                const auto position = std::ranges::find_if(versions, [&](const auto& v) { return v.first.commit == run.commit; });
                if (!worse || size_t(position - versions.begin()) < sinceIndex)
                    continue;
                const auto older = shift.index > 0 ? points[shift.index - 1].run.all("Code") : std::vector<std::string>{};
                auto code = codeCells(run, shift.index > 0 ? &older : nullptr);
                rows.push_back({{rowLabel(row), "light"}, {row.metric, "mut"}, {run.trailer("Run"), "a2"},
                    {std::format("{:+.1f}%", (shift.shift.ratio - 1) * 100), "err"}, {"▼", "err"},
                    {std::format("{:.3g}", shift.shift.p), "mut"}, std::move(code.front())});
            }
        }
    }
    if (rows.empty()) {
        Logger::message(TextFormatter::format("<ok>no regressions</>"));
        return 0;
    }
    printTable({"bench", "metric", "since", "shift", "", "p", "code"}, rows, {3, 5});
    return 1;
}

/// Baselines are git tags `baseline/<name>` of the results repository, like `git tag`:
/// no name lists them, a name sets one (on a revision, default the last run), -d deletes.
int tag(const Context& context, const Arguments& arguments) {
    auto repository = context.repository();
    if (const auto name = arguments.option("--delete"); !name.empty()) {
        repository.removeBaseline(name);
        Logger::message(TextFormatter::format("<a2><b>deleted<//> <light>{}</>", name));
        return 0;
    }
    if (arguments.positional.empty()) {
        std::vector<std::vector<Cell>> rows;
        const auto runs = repository.runs();
        for (const auto& [name, commit] : repository.baselines()) {
            std::string id, date;
            for (const auto& run : runs)
                if (run.commit == commit) {
                    id = run.trailer("Run");
                    date = run.date.substr(0, 16);
                    std::ranges::replace(date, 'T', ' ');
                }
            rows.push_back({{name, "wrn"}, {id, "a2"}, {date, "mut"}, {commit.substr(0, 10), "mut"}});
        }
        if (rows.empty())
            Logger::message(TextFormatter::format("<mut>no tags; set one: lbm tag <name> [rev]</>"));
        else
            printTable({"tag", "run", "date", "commit"}, rows, {});
        return 0;
    }
    const std::string name = arguments.at(0);
    const std::string revision = arguments.at(1, "HEAD");
    repository.setBaseline(name, revision);
    std::string id;
    const std::string commit = repository.resolve(revision);
    for (const auto& run : repository.runs())
        if (run.commit == commit)
            id = run.trailer("Run");
    Logger::message(TextFormatter::format("<a2><b>tagged<//> <light>{}</> <mut>→ {} {}</>", name, id, commit.substr(0, 10)));
    return 0;
}

}

int main(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h") {
        Logger::message(TextFormatter::format("{}", Usage));
        return argc < 2 ? 2 : 0;
    }
    try {
        const std::string command = argv[1];
        const Arguments arguments = parse(argc, argv, 2);
        Context context{.config = Lbm::Config::load(fs::current_path())};
        if (const auto results = arguments.option("--results"); !results.empty())
            context.config.results = fs::absolute(results);
        context.machine = arguments.option("--machine", command == "list" ? std::string{} : Bh::currentMachine().id);

        if (command == "list") return list(context, arguments);
        if (command == "run") return run(context, arguments);
        if (command == "log") return log(context, arguments);
        if (command == "show") return show(context, arguments);
        if (command == "compare") return compare(context, arguments);
        if (command == "history") return history(context, arguments);
        if (command == "regressions") return regressions(context, arguments);
        if (command == "tag") return tag(context, arguments);
        throw Error("unknown command '{}' (lbm --help)", command);
    } catch (const Lattice::ExceptionBase& error) {
        Logger::error("lbm", "{}", error.what());
        return 2;
    }
}
