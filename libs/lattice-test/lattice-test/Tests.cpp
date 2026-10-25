#include "Tests.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace Lattice {
namespace {
struct TestFailure { Testing::Diagnostic diagnostic; bool recorded; };
struct TestSkipped { std::string reason; };
thread_local Testing::TestResult* active = nullptr;

void assertion(bool condition, bool fatal, const char* expression, const char* file, int line, std::string_view message) {
    if (condition) return;
    Testing::Diagnostic diagnostic{fatal ? "REQUIRE" : "CHECK", expression, file, std::string(message), line};
    if (active) active->diagnostics.push_back(diagnostic);
    // Outside a test (a worker thread) the failure travels as an exception to the test's thread.
    if (fatal || !active) throw TestFailure{std::move(diagnostic), active != nullptr};
}
}

void testRequire(bool value, const char* expression, const char* file, int line, std::string_view message) {
    assertion(value, true, expression, file, line, message);
}
void testCheck(bool value, const char* expression, const char* file, int line, std::string_view message) {
    assertion(value, false, expression, file, line, message);
}
void testSkip(std::string_view reason) { throw TestSkipped{std::string(reason)}; }

Test& Test::instance() { static Test registry; return registry; }

std::vector<Testing::TestInfo> Test::list(std::string_view filter) const {
    std::vector<Testing::TestInfo> result;
    for (const auto& test : tests_)
        if (Testing::matches(filter, test.name)) result.push_back({test.name, test.description});
    return result;
}

Testing::RunResult Test::run(std::string_view filter, const Callbacks& callbacks) const {
    using Clock = std::chrono::steady_clock;
    const auto begin = Clock::now();
    Testing::RunResult report{.registered = tests_.size()};
    const auto selected = list(filter).size();
    // A copy: callbacks and tests may register more cases.
    const auto tests = tests_;
    for (const auto& test : tests) {
        if (!Testing::matches(filter, test.name)) continue;
        Testing::TestResult result{.test = {test.name, test.description}, .status = Testing::Status::Passed};
        if (callbacks.start) callbacks.start(result.test, report.tests.size(), selected);
        const auto start = Clock::now();
        {
            struct Restore {
                Testing::TestResult* previous = active;
                ~Restore() { active = previous; }
            } restore;
            active = &result;
            try {
                auto fixture = test.createFixture();
                if (!fixture) throw std::runtime_error("fixture factory returned null");
                test.function(*fixture);
            } catch (const TestFailure& error) {
                if (!error.recorded) result.diagnostics.push_back(error.diagnostic);
            } catch (const TestSkipped& skip) {
                result.status = Testing::Status::Skipped;
                result.skipReason = skip.reason;
            } catch (const std::exception& error) {
                result.diagnostics.push_back({.kind = "exception", .message = error.what()});
            } catch (...) {
                result.diagnostics.push_back({.kind = "exception", .message = "unknown exception"});
            }
        }
        if (!result.diagnostics.empty()) result.status = Testing::Status::Failed;
        result.elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        report.tests.push_back(std::move(result));
        if (callbacks.result) callbacks.result(report.tests.back());
    }
    report.elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
    return report;
}
}

namespace Lattice::Testing {

size_t RunResult::count(Status status) const {
    return std::ranges::count(tests, status, &TestResult::status);
}

int RunResult::exitCode() const { return tests.empty() ? 2 : count(Status::Failed) ? 1 : 0; }

bool matches(std::string_view filter, std::string_view name) {
    if (filter.empty()) filter = "*";
    size_t p = 0, n = 0, star = std::string_view::npos, retry = 0;
    while (n < name.size()) {
        if (p < filter.size() && (filter[p] == '?' || filter[p] == name[n])) { ++p; ++n; }
        else if (p < filter.size() && filter[p] == '*') { star = p++; retry = n; }
        else if (star != std::string_view::npos) { p = star + 1; n = ++retry; }
        else return false;
    }
    while (p < filter.size() && filter[p] == '*') ++p;
    return p == filter.size();
}

std::string_view statusName(Status status) {
    switch (status) {
        case Status::Listed: return "listed";
        case Status::Passed: return "passed";
        case Status::Failed: return "failed";
        case Status::Skipped: return "skipped";
    }
    return "unknown";
}
}
