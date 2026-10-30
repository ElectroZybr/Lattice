#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <lattice-test/Tests.hpp>

namespace Lattice::Testing {

struct Suite {
    std::string name;
    std::filesystem::path binary, directory;
    std::string target; // xmake target, empty for a plain executable
};

struct SuiteOptions {
    std::string filter = "*";
    bool list = false;
    bool verbose = false;
    std::chrono::milliseconds timeout{30000};
    /// Empty: the process log and report live in a temporary directory removed after the run.
    std::filesystem::path logs;
    /// Live process output, unformatted.
    std::function<void(std::string_view)> output;
};

struct SuiteResult {
    Suite suite;
    RunResult report;
    int exitCode = 2;
    std::string error;
    /// The end of the process log when the suite could not report: loader errors, crashes.
    std::string output;
    /// The kept process log, with SuiteOptions::logs only.
    std::filesystem::path log;
};

/// An xmake project: its binary `*.tests` targets (stale build files are not suites).
/// Otherwise executable `*.tests` files under the directory. Never runs anything.
std::vector<Suite> discover(const std::filesystem::path& directory);
/// Runs the executable without a shell, in its project directory, with a timeout.
SuiteResult runSuite(const Suite& suite, const SuiteOptions& options = {});
void buildSuite(const Suite& suite, std::chrono::milliseconds timeout = std::chrono::milliseconds{120000});
std::string suitesJson(const std::vector<SuiteResult>& results);
/// Text without terminal escape codes.
std::string plain(std::string_view text);

}
