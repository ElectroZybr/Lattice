#include <lattice-test/Tests.hpp>
#include <lattice-test/Report.hpp>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace T = Lattice::Testing;
namespace {
int constructed = 0, destroyed = 0, continued = 0;
struct Tracked : Lattice::Fixture {
    Tracked() { ++constructed; }
    ~Tracked() override { ++destroyed; }
};
struct Broken : Lattice::Fixture { Broken() { throw std::runtime_error("constructor failure"); } };
std::unique_ptr<Lattice::Fixture> tracked() { return std::make_unique<Tracked>(); }
std::unique_ptr<Lattice::Fixture> broken() { return std::make_unique<Broken>(); }
void pass(Lattice::Fixture&) { REQUIRE(true); }
void check(Lattice::Fixture&) { CHECK(false, "detail"); ++continued; }
void require(Lattice::Fixture&) { REQUIRE(false); ++continued; }
void skip(Lattice::Fixture&) { SKIP("no device"); }
void exception(Lattice::Fixture&) { throw std::runtime_error("error"); }
void unknown(Lattice::Fixture&) { throw 42; }
void failedSkip(Lattice::Fixture&) { CHECK(false); SKIP("cannot hide a failure"); }

TEST(Runner_ResultsAndCallbacks, Lattice::Fixture) {
    constructed = destroyed = continued = 0;
    Lattice::Test runner;
    runner.add({"pass", "", pass, tracked});
    runner.add({"check", "", check, tracked});
    runner.add({"require", "", require, tracked});
    runner.add({"skip", "", skip, tracked});
    runner.add({"exception", "", exception, tracked});
    runner.add({"unknown", "", unknown, tracked});
    runner.add({"construction", "", pass, broken});
    runner.add({"failedSkip", "", failedSkip, tracked});
    runner.add({"last", "", pass, tracked});
    REQUIRE(runner.list("*").size() == 9);
    REQUIRE(constructed == 0);
    size_t starts = 0, results = 0;
    const auto report = runner.run("*", {
        .start = [&](const auto&, size_t index, size_t selected) { REQUIRE(index == starts++); REQUIRE(selected == 9); },
        .result = [&](const auto&) { ++results; },
    });
    REQUIRE(starts == 9 && results == 9);
    REQUIRE(report.count(T::Status::Passed) == 2);
    REQUIRE(report.count(T::Status::Failed) == 6);
    REQUIRE(report.count(T::Status::Skipped) == 1);
    REQUIRE(report.exitCode() == 1);
    REQUIRE(constructed == 8 && destroyed == 8 && continued == 1);
    REQUIRE(report.tests[1].diagnostics[0].message == "detail");
    REQUIRE(report.tests[1].diagnostics[0].line > 0);
    REQUIRE(report.tests[3].skipReason == "no device");
    REQUIRE(runner.run("missing").exitCode() == 2);
    REQUIRE(runner.run("pass").exitCode() == 0);
}
TEST(Runner_NestedRunRestoresAssertions, Lattice::Fixture) {
    Lattice::Test runner;
    runner.add({"require", "", require, tracked});
    REQUIRE(runner.run().count(T::Status::Failed) == 1);
    REQUIRE(true); // This assertion still belongs to the outer test.
}
TEST(Results_Filters, Lattice::Fixture) {
    REQUIRE(T::matches("*", "anything"));
    REQUIRE(T::matches("a?c*", "abc-long"));
    REQUIRE(!T::matches("a?c", "ac"));
    REQUIRE(!T::matches("pass", "passExtra"));
    REQUIRE(T::matches("*end", "a-start-end"));
    REQUIRE(!T::matches("*end", "a-start-ending"));
}
TEST(Report_RoundTrip, Lattice::Fixture) {
    Lattice::Test runner;
    runner.add({"name\t\"\\\n", "description\r\n", check, tracked});
    const auto report = runner.run();
    const auto decoded = T::readReport(T::writeReport(report));
    REQUIRE(decoded.tests[0].test.name == report.tests[0].test.name);
    REQUIRE(decoded.tests[0].test.description == report.tests[0].test.description);
    REQUIRE(decoded.tests[0].diagnostics[0].message == "detail");
    REQUIRE(T::writeJson(decoded).find("\\u0009") != std::string::npos);
    REQUIRE(T::jsonString("\x01\t\n\"\\") == "\"\\u0001\\u0009\\u000a\\\"\\\\\"");
    for (const auto* input : {"", "other", "lattice-test-v1\t1\t0\n", "lattice-test-v1\t1\t0\nend\t1\n"}) {
        bool refused = false;
        try { (void)T::readReport(input); } catch (const std::exception&) { refused = true; }
        REQUIRE(refused);
    }
}
}
