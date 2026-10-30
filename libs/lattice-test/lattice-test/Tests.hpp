#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Visibility.hpp"
#include "lattice-tools/Fixture.hpp"


#define TEST2(name, FixtureType) \
    TEST3(name, FixtureType, "")

#define TEST3(name, FixtureType, description) \
    static void name(FixtureType& fixture); \
    static std::unique_ptr<::Lattice::Fixture> _fixture_##name() { \
        return std::make_unique<FixtureType>(); \
    } \
    static void _run_##name(::Lattice::Fixture& fixture) { \
        name(static_cast<FixtureType&>(fixture)); \
    } \
    static ::Lattice::TestRegistrar _test_##name(#name, description, _run_##name, _fixture_##name); \
    static void name(FixtureType& fixture)

#define TEST_SELECT(_1, _2, _3, NAME, ...) NAME
#define TEST(...) TEST_SELECT(__VA_ARGS__, TEST3, TEST2)(__VA_ARGS__)

#define REQUIRE(condition, ...) \
    ::Lattice::testRequire(static_cast<bool>(condition), #condition, __FILE__, __LINE__ __VA_OPT__(, __VA_ARGS__))

#define CHECK(condition, ...) \
    ::Lattice::testCheck(static_cast<bool>(condition), #condition, __FILE__, __LINE__ __VA_OPT__(, __VA_ARGS__))

#define SKIP(reason) ::Lattice::testSkip(reason)


namespace Lattice::Testing {

enum class Status { Listed, Passed, Failed, Skipped };

struct Diagnostic {
    std::string kind, expression, file, message;
    int line = 0;
};

struct TestInfo { std::string name, description; };

struct TestResult {
    TestInfo test;
    Status status = Status::Listed;
    uint64_t elapsedNs = 0;
    std::vector<Diagnostic> diagnostics;
    std::string skipReason;
};

struct LT_TEST RunResult {
    size_t registered = 0;
    uint64_t elapsedNs = 0;
    std::vector<TestResult> tests;
    size_t count(Status status) const;
    /// 0 — passed or listed, 1 — failures, 2 — nothing selected.
    int exitCode() const;
};

/// Glob with `*` and `?`; an empty filter matches everything.
LT_TEST bool matches(std::string_view filter, std::string_view name);
LT_TEST std::string_view statusName(Status status);

}

namespace Lattice {

struct TestCase {
    std::string name;
    std::string description;
    void (*function)(Fixture&);
    std::unique_ptr<Fixture> (*createFixture)();
};

/// Registry and in-process execution. Never writes to the console: presentation is the
/// caller's (the standard main of lattice-test-main, ltest, a GUI).
class LT_TEST Test {
public:
    static Test& instance();

    void add(TestCase test) { tests_.push_back(std::move(test)); }
    const std::vector<TestCase>& tests() const { return tests_; }

    struct Callbacks {
        std::function<void(const Testing::TestInfo&, size_t index, size_t selected)> start;
        std::function<void(const Testing::TestResult&)> result;
    };
    /// Matching cases without constructing fixtures.
    std::vector<Testing::TestInfo> list(std::string_view filter = "*") const;
    Testing::RunResult run(std::string_view filter = "*", const Callbacks& callbacks = {}) const;

private:
    std::vector<TestCase> tests_;
};

class TestRegistrar {
public:
    TestRegistrar(std::string_view name, std::string_view description, void (*function)(Fixture&),
                  std::unique_ptr<Fixture> (*createFixture)()) {
        Test::instance().add({std::string(name), std::string(description), function, createFixture});
    }
};

LT_TEST void testRequire(bool condition, const char* expression, const char* file, int line, std::string_view message = {});
LT_TEST void testCheck(bool condition, const char* expression, const char* file, int line, std::string_view message = {});
[[noreturn]] LT_TEST void testSkip(std::string_view reason);

}
