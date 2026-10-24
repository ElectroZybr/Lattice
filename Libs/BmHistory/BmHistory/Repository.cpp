#include "Repository.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <set>

#include <Lattice/Tools/Exception.hpp>

namespace Lattice::BmHistory {

using Error = Exception<Repository>;

namespace {

constexpr std::string_view BaselinePrefix = "baseline/";

std::string trimmed(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw Error("cannot write '{}'", path.string());
    out << text;
}

/// A suite name is a file name: no folders.
std::string fileName(std::string_view name) {
    std::string result(name);
    for (char& c : result)
        if (c == '/' || c == '\\' || c == ':')
            c = '_';
    return result.empty() ? "suite" : result;
}

bool isRunId(std::string_view revision) {
    return revision.size() > 1 && revision[0] == 'r' &&
           std::all_of(revision.begin() + 1, revision.end(), [](char c) { return c >= '0' && c <= '9'; });
}

}

std::string RunInfo::trailer(std::string_view key) const {
    for (const auto& [name, value] : trailers)
        if (name == key)
            return value;
    return {};
}

std::vector<std::string> RunInfo::all(std::string_view key) const {
    std::vector<std::string> values;
    for (const auto& [name, value] : trailers)
        if (name == key)
            values.push_back(value);
    return values;
}

Repository Repository::open(const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    Repository repository(std::filesystem::absolute(root));
    if (!std::filesystem::exists(root / ".git")) {
        repository.git_.run({"init", "--quiet"});
        repository.git_.run({"config", "commit.gpgsign", "false"});
        writeFile(root / "README.md",
            "# Benchmark results\n\nWritten by lattice-benchmarks (`lbm`): `<machine>/<suite>.tsv` per suite, a commit per saved run,\n"
            "baselines as tags `baseline/<name>`. See Lattice/Docs/ru/BmHistory.md.\n");
    }
    // Commits need an identity; a repository of results should not depend on the user's.
    if (repository.git_.call({"config", "user.email"}).status != 0) {
        repository.git_.run({"config", "user.name", "lbm"});
        repository.git_.run({"config", "user.email", "lbm@localhost"});
    }
    return repository;
}

std::string Repository::tablePath(std::string_view machine, std::string_view suite) {
    return std::format("{}/{}.tsv", fileName(machine), fileName(suite));
}

Benchmarks::ResultTable mergeRun(const Benchmarks::ResultTable& saved, const Benchmarks::ResultTable& run,
                                 std::string_view runId) {
    // Benches the run measured, and the works (with mode) whose comparison it redid.
    std::set<std::pair<std::string, std::string>> benches;
    std::set<std::tuple<std::string, std::string, std::string>> works;
    for (const auto& row : run.rows) {
        if (row.bench != "-")
            benches.insert({row.group, row.bench});
        if (row.work != "-")
            works.insert({row.group, row.work, row.mode});
    }

    Benchmarks::ResultTable merged;
    merged.meta = run.meta;
    for (const auto& row : saved.rows) {
        const bool replaced = row.bench == "-"
            ? works.contains({row.group, row.work, row.mode})
            : benches.contains({row.group, row.bench});
        if (!replaced)
            merged.rows.push_back(row);
    }
    for (auto row : run.rows) {
        row.measured = std::string(runId);
        merged.rows.push_back(std::move(row));
    }
    Benchmarks::sortRows(merged.rows);
    return merged;
}

RunInfo Repository::save(const SaveRequest& request) {
    if (request.machine.empty() || request.suite.empty())
        throw Error("a run needs a machine and a suite");
    if (request.table.rows.empty())
        throw Error("run of '{}' has no results", request.suite);

    const auto count = git_.call({"rev-list", "--count", "HEAD"});
    const size_t number = count.status == 0 ? std::stoull(trimmed(count.out)) + 1 : 1;
    const std::string runId = std::format("r{:04}", number);

    const std::string path = tablePath(request.machine, request.suite);
    const auto file = root() / path;
    Benchmarks::ResultTable saved;
    if (std::filesystem::exists(file))
        saved = Benchmarks::parseTsv(readFile(file));
    auto merged = mergeRun(saved, request.table, runId);
    // Run metadata lives in the commit; the file keeps only what describes its rows.
    merged.meta.clear();
    writeFile(file, Benchmarks::writeTsv(merged));

    const auto machineFile = root() / fileName(request.machine) / "machine.toml";
    if (!request.machineDescription.empty() &&
        (!std::filesystem::exists(machineFile) || readFile(machineFile) != request.machineDescription))
        writeFile(machineFile, request.machineDescription);

    std::set<std::pair<std::string, std::string>> benches;
    for (const auto& row : request.table.rows)
        if (row.bench != "-")
            benches.insert({row.group, row.bench});
    std::string message = request.message.empty()
        ? std::format("lbm: {} ({} bench{}), {}", request.suite, benches.size(), benches.size() == 1 ? "" : "es", runId)
        : std::format("{}, {}", request.message, runId);
    message += std::format("\n\nMachine: {}\nSuite: {}\nRun: {}\n", request.machine, request.suite, runId);
    for (const auto& [key, value] : request.trailers)
        message += std::format("{}: {}\n", key, value);

    git_.run({"add", "--all", "."});
    git_.run({"commit", "--quiet", "--allow-empty", "-F", "-"}, message);
    return runs().front();
}

std::vector<RunInfo> Repository::log(const std::vector<std::string>& extra) const {
    if (git_.call({"rev-parse", "--verify", "--quiet", "HEAD"}).status != 0)
        return {};
    std::vector<std::string> arguments{"log", "--format=%H%x1f%cI%x1f%s%x1f%(trailers:unfold,separator=%x1e)%x1d"};
    arguments.insert(arguments.end(), extra.begin(), extra.end());
    const std::string out = git_.run(arguments);

    std::vector<RunInfo> runs;
    size_t at = 0;
    while (at < out.size()) {
        const size_t end = out.find('\x1d', at);
        std::string_view record(out.data() + at, (end == std::string::npos ? out.size() : end) - at);
        at = end == std::string::npos ? out.size() : end + 1;
        while (!record.empty() && (record.front() == '\n' || record.front() == '\r'))
            record.remove_prefix(1);
        if (record.empty())
            continue;

        std::vector<std::string_view> fields;
        for (size_t start = 0;;) {
            const size_t separator = record.find('\x1f', start);
            fields.push_back(record.substr(start, separator == std::string_view::npos ? std::string_view::npos
                                                                                      : separator - start));
            if (separator == std::string_view::npos)
                break;
            start = separator + 1;
        }
        if (fields.size() < 4)
            continue;
        RunInfo run{.commit = std::string(fields[0]), .date = std::string(fields[1]), .subject = std::string(fields[2])};
        std::string_view trailers = fields[3];
        for (size_t start = 0; start < trailers.size();) {
            const size_t separator = trailers.find('\x1e', start);
            std::string_view entry = trailers.substr(start, separator == std::string_view::npos ? std::string_view::npos
                                                                                                : separator - start);
            start = separator == std::string_view::npos ? trailers.size() : separator + 1;
            const size_t colon = entry.find(':');
            if (colon == std::string_view::npos)
                continue;
            std::string_view value = entry.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
            while (!value.empty() && (value.back() == '\n' || value.back() == ' ')) value.remove_suffix(1);
            run.trailers.emplace_back(std::string(entry.substr(0, colon)), std::string(value));
        }
        runs.push_back(std::move(run));
    }
    return runs;
}

std::vector<RunInfo> Repository::runs(std::string_view machine, std::string_view suite) const {
    std::vector<RunInfo> result;
    for (auto& run : log({}))
        if ((machine.empty() || run.trailer("Machine") == machine) && (suite.empty() || run.trailer("Suite") == suite))
            result.push_back(std::move(run));
    return result;
}

std::string Repository::resolve(std::string_view revision) const {
    if (isRunId(revision)) {
        for (const auto& run : log({}))
            if (run.trailer("Run") == revision)
                return run.commit;
        throw Error("no saved run '{}'", revision);
    }
    const auto result = git_.call({"rev-parse", "--verify", "--quiet", std::string(revision) + "^{commit}"});
    if (result.status != 0) {
        if (git_.call({"rev-parse", "--verify", "--quiet", std::string(BaselinePrefix) + std::string(revision)}).status == 0)
            return resolve(std::string(BaselinePrefix) + std::string(revision));
        throw Error("unknown revision '{}'", revision);
    }
    return trimmed(result.out);
}

std::optional<Benchmarks::ResultTable> Repository::load(std::string_view revision, std::string_view machine,
                                                        std::string_view suite) const {
    const auto contents = git_.show({resolve(revision) + ":" + tablePath(machine, suite)});
    if (!contents.front())
        return std::nullopt;
    return Benchmarks::parseTsv(*contents.front());
}

std::vector<std::pair<RunInfo, Benchmarks::ResultTable>> Repository::history(std::string_view machine,
                                                                            std::string_view suite) const {
    const std::string path = tablePath(machine, suite);
    auto runs = log({"--", path});
    std::ranges::reverse(runs);
    std::vector<std::string> objects;
    objects.reserve(runs.size());
    for (const auto& run : runs)
        objects.push_back(run.commit + ":" + path);
    const auto contents = git_.show(objects);

    std::vector<std::pair<RunInfo, Benchmarks::ResultTable>> versions;
    for (size_t i = 0; i < runs.size(); ++i)
        if (contents[i])
            versions.emplace_back(std::move(runs[i]), Benchmarks::parseTsv(*contents[i]));
    return versions;
}

void Repository::setBaseline(std::string_view name, std::string_view revision) {
    if (name.empty())
        throw Error("a baseline needs a name");
    git_.run({"tag", "--force", std::string(BaselinePrefix) + std::string(name), resolve(revision)});
}

void Repository::removeBaseline(std::string_view name) {
    git_.run({"tag", "--delete", std::string(BaselinePrefix) + std::string(name)});
}

std::vector<std::pair<std::string, std::string>> Repository::baselines() const {
    std::vector<std::pair<std::string, std::string>> result;
    const std::string out = git_.run({"tag", "--list", std::string(BaselinePrefix) + "*",
                                      "--format=%(refname:short)%09%(objectname)"});
    size_t at = 0;
    while (at < out.size()) {
        const size_t end = out.find('\n', at);
        const std::string_view line(out.data() + at, (end == std::string::npos ? out.size() : end) - at);
        at = end == std::string::npos ? out.size() : end + 1;
        const size_t tab = line.find('\t');
        if (tab == std::string_view::npos)
            continue;
        result.emplace_back(std::string(line.substr(BaselinePrefix.size(), tab - BaselinePrefix.size())),
                            std::string(line.substr(tab + 1)));
    }
    return result;
}

}
