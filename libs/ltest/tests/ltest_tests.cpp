// ltest against a probe suite: protocol, isolation, crashes, timeouts and discovery.
#include <chrono>
#include <fstream>

#include <lattice-test/Tests.hpp>

#include "ltest/Suites.hpp"

namespace T = Lattice::Testing;
namespace fs = std::filesystem;
namespace {

struct Directory : Lattice::Fixture {
    fs::path path = fs::temp_directory_path() /
        ("ltest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() { fs::create_directories(path); }
    ~Directory() override { std::error_code error; fs::remove_all(path, error); }
};

T::Suite probe(const fs::path& directory) { return {"probe", LTEST_PROBE, directory, {}}; }

size_t temporaryRuns() {
    size_t runs = 0;
    for (const auto& entry : fs::directory_iterator(fs::temp_directory_path()))
        runs += entry.path().filename().string().starts_with("run-");
    return runs;
}

TEST(Suites_ProtocolAndIsolation, Directory) {
    const auto suite = probe(fixture.path);
    T::SuiteOptions options{.filter = "Probe_Pass", .logs = fixture.path / "logs"};
    const auto passed = T::runSuite(suite, options);
    REQUIRE(passed.error.empty(), passed.error);
    REQUIRE(passed.exitCode == 0 && passed.report.tests.size() == 1);
    std::ifstream log(passed.log);
    const std::string raw{std::istreambuf_iterator<char>(log), {}};
    REQUIRE(raw.find("native stdout") != std::string::npos && raw.find("native stderr") != std::string::npos);

    options.filter = "Probe_Fail";
    const auto failed = T::runSuite(suite, options);
    REQUIRE(failed.error.empty(), failed.error);
    REQUIRE(failed.exitCode == 1 && failed.report.tests[0].diagnostics.size() == 1);
    REQUIRE(failed.log != passed.log, "every run keeps its own log");

    options.filter = "Probe_Skip";
    REQUIRE(T::runSuite(suite, options).report.count(T::Status::Skipped) == 1);
    options.filter = "missing";
    const auto missing = T::runSuite(suite, options);
    REQUIRE(missing.error.empty() && missing.exitCode == 2, missing.error);
    options.filter = "Probe_*";
    options.list = true;
    const auto listed = T::runSuite(suite, options);
    REQUIRE(listed.error.empty(), listed.error);
    REQUIRE(listed.report.count(T::Status::Listed) == 5, "listing constructs no fixtures and runs nothing");
}

TEST(Suites_LeaveNothingBehindByDefault, Directory) {
    const auto before = temporaryRuns();
    const auto result = T::runSuite(probe(fixture.path), {.filter = "Probe_Pass"});
    REQUIRE(result.error.empty() && result.exitCode == 0, result.error);
    REQUIRE(result.log.empty() && temporaryRuns() == before, "the temporary run directory is removed");
    REQUIRE(!fs::exists(fixture.path / ".ltest"), "no history in the project");
}

TEST(Suites_CrashTimeoutAndLaunchErrors, Directory) {
    auto suite = probe(fixture.path);
    T::SuiteOptions options{.filter = "Probe_Crash"};
    const auto crash = T::runSuite(suite, options);
    REQUIRE(!crash.error.empty() && crash.exitCode != 0);
    REQUIRE(crash.output.find("about to abort") != std::string::npos, "the end of the process log explains the crash");
    options.filter = "Probe_Timeout";
    options.timeout = std::chrono::milliseconds(50);
    const auto timeout = T::runSuite(suite, options);
    REQUIRE(timeout.exitCode == 124 && timeout.error.find("timeout") != std::string::npos);
    suite.binary = fixture.path / "does not exist;no shell";
    REQUIRE(!T::runSuite(suite, options).error.empty());
}

TEST(Suites_VerboseStreamsBeforeCompletion, Directory) {
    const auto begin = std::chrono::steady_clock::now();
    bool early = false;
    std::string live;
    T::SuiteOptions options{.filter = "Probe_Timeout", .verbose = true, .timeout = std::chrono::milliseconds(500)};
    options.output = [&](std::string_view chunk) {
        live += chunk;
        if (live.find("Probe_Timeout") != std::string::npos &&
            std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(400)) early = true;
    };
    const auto result = T::runSuite(probe(fixture.path), options);
    REQUIRE(result.exitCode == 124);
    REQUIRE(early, "progress arrives while the test is still running");
    REQUIRE(live.find("[1/1]") != std::string::npos);
}

TEST(Suites_DirectoryDiscovery, Directory) {
    fs::create_symlink(LTEST_PROBE, fixture.path / "with spaces;literal.tests");
    fs::create_directories(fixture.path / ".hidden");
    fs::create_symlink(LTEST_PROBE, fixture.path / ".hidden/ignored.tests");
    const auto suites = T::discover(fixture.path);
    REQUIRE(suites.size() == 1 && suites[0].name == "with spaces;literal");
    const auto result = T::runSuite(suites[0], {.filter = "Probe_Pass"});
    REQUIRE(result.exitCode == 0, result.error);
}

TEST(Suites_JsonHasNoTerminalCodes, Lattice::Fixture) {
    T::SuiteResult suite{.suite = {"suite"}, .exitCode = 2, .error = "exited 127 without a report",
                         .output = "\x1b[31mloader error\x1b[0m"};
    const auto json = T::suitesJson({suite});
    REQUIRE(json.find('\x1b') == std::string::npos && json.find("loader error") != std::string::npos);
}
}
