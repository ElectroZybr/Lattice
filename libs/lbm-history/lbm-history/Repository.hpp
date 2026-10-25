#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <lbm-runner/Results.hpp>

#include "Git.hpp"

namespace Lattice::BmHistory {

/// Default folder of the results repository, created in the folder `lbm` runs in.
inline constexpr std::string_view DefaultFolder = "lbm-results";

/// One saved run: a commit of the results repository.
struct RunInfo {
    std::string commit;   // full sha
    std::string date;     // ISO 8601
    std::string subject;
    std::vector<std::pair<std::string, std::string>> trailers;

    /// First trailer value with this key; empty when there is none.
    std::string trailer(std::string_view key) const;
    /// Every value of a repeated trailer (`Code:`).
    std::vector<std::string> all(std::string_view key) const;
};

/// What `save` records.
struct SaveRequest {
    std::string machine;            // Machine::id: the folder
    std::string machineDescription; // machine.toml, written when it changes
    std::string suite;              // file `<machine>/<suite>.tsv`
    Benchmarks::ResultTable table;  // rows of this run (measured = `-`)
    std::vector<std::pair<std::string, std::string>> trailers;   // Code:, Build:, Filter:, …
    std::string message;            // first line; generated when empty
};

/**
 Git repository of benchmark results: `<machine>/<suite>.tsv` and `<machine>/machine.toml`,
 a commit per saved run with its environment in trailers, baselines as tags
 `baseline/<name>`. Git stores and navigates; deciding what is a regression is
 Statistics' job.
*/
class Repository {
public:
    /// Open the repository at `root`, creating the folder and `git init` when missing.
    static Repository open(const std::filesystem::path& root);

    const std::filesystem::path& root() const noexcept { return git_.directory(); }

    /// Save a run: rows of the benches it ran replace their old rows, other rows stay
    /// (partial runs); new rows get `measured` = the run id; one commit.
    RunInfo save(const SaveRequest& request);

    /// Saved runs, newest first; optionally of one machine and suite.
    std::vector<RunInfo> runs(std::string_view machine = {}, std::string_view suite = {}) const;

    /// The results table of a suite at a revision (sha, tag, `HEAD~2`, run id `r0007`).
    std::optional<Benchmarks::ResultTable> load(std::string_view revision, std::string_view machine,
                                                std::string_view suite) const;

    /// Every saved version of a suite's table, oldest first.
    std::vector<std::pair<RunInfo, Benchmarks::ResultTable>> history(std::string_view machine,
                                                                    std::string_view suite) const;

    /// Full sha of a revision; a run id (`r0007`) names the run that recorded it.
    std::string resolve(std::string_view revision) const;

    void setBaseline(std::string_view name, std::string_view revision = "HEAD");
    void removeBaseline(std::string_view name);
    /// Baselines: name and commit.
    std::vector<std::pair<std::string, std::string>> baselines() const;

    /// Path of a suite's table inside the repository.
    static std::string tablePath(std::string_view machine, std::string_view suite);

private:
    explicit Repository(std::filesystem::path root) : git_(std::move(root)) {}
    std::vector<RunInfo> log(const std::vector<std::string>& extra) const;

    Git git_;
};

/// Merge a partial run into the saved table: rows of the works the run measured (every
/// variant and their comparison) are replaced; `measured` of the new rows is `run`.
Benchmarks::ResultTable mergeRun(const Benchmarks::ResultTable& saved, const Benchmarks::ResultTable& run,
                                 std::string_view runId);

}
