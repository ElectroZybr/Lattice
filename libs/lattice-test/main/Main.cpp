// The standard main of a test executable: linked from the static lattice-test-main, so a
// suite is only its TEST cases. A suite with its own main simply does not pull this one in.
#include <fstream>
#include <iostream>
#include <memory>

#include <lattice-logger/LogScope.hpp>
#include <lattice-logger/Logger.hpp>
#include <Lattice/tools/Exception.hpp>

#include <lattice-test/Report.hpp>
#include <lattice-test/Tests.hpp>

namespace {
using namespace Lattice::Testing;
struct Main;
using Error = Lattice::Exception<Main>;

constexpr std::string_view Usage =
    "[filter] [--list] [--json] [-v|--verbose] [--report <file>]\n"
    "  filter          test names, * and ? (quote wildcards); default *\n"
    "  --list          matching names without running them\n"
    "  --json          results as JSON in stdout\n"
    "  -v, --verbose   live progress of every test\n"
    "  --report <file> results in the ltest protocol";

void diagnostics(const TestResult& test) {
    for (const auto& error : test.diagnostics)
        Logger::message("  <err><b>{}</></><mut>: </><err>{}</><mut>{}</><a2>{}</><mut>{}</><err>{}</>",
                        error.kind, error.expression, error.file.empty() ? "" : " (",
                        error.file.empty() ? "" : error.file + ":" + std::to_string(error.line),
                        error.file.empty() ? "" : ")", error.message.empty() ? "" : ": " + error.message);
}

void failures(const RunResult& run) {
    for (const auto& test : run.tests) {
        if (test.status == Status::Failed) {
            Logger::message("<err><b>failed {}</></>", test.test.name);
            diagnostics(test);
        } else if (test.status == Status::Skipped) {
            Logger::message("<wrn><b>skipped {}</></><wrn>: {}</>", test.test.name, test.skipReason);
        }
    }
}

void summary(const RunResult& run) {
    Logger::message("<ok><b>{} passed</></><mut>, </><err><b>{} failed</></><mut>, </><wrn>{} skipped</><mut>; {}/{} selected ({:.1f} ms)</>",
                    run.count(Status::Passed), run.count(Status::Failed), run.count(Status::Skipped),
                    run.tests.size(), run.registered, run.elapsedNs / 1000000.0);
}

/// Live progress: a log scope per test, opened before its fixture.
class Progress {
public:
    Progress() : previous_(LogSystem::current().defaultMode()) {
        Logger::setDefaultMode(LogMode::Verbose | LogMode::Gap);
    }
    ~Progress() { test_.reset(); Logger::setDefaultMode(previous_); }

    Lattice::Test::Callbacks callbacks() {
        return {
            .start = [this](const TestInfo& test, size_t index, size_t selected) {
                // format_string is consteval: the scope cannot be built through make_unique.
                test_.reset(new LogScope("Test", LogMode::Verbose, "<mut>[{}/{}]</> <a2><b>'{}'</></>", index + 1, selected, test.name));
            },
            .result = [this](const TestResult& test) {
                if (test.status == Status::Failed) {
                    diagnostics(test);
                    test_->finishError("<err>'{}' failed</>", test.test.name);
                } else if (test.status == Status::Skipped) {
                    test_->finish("<wrn>'{}' skipped: {}</>", test.test.name, test.skipReason);
                } else test_->finish("<ok><b>'{}' passed</></>", test.test.name);
                test_.reset();
                std::cout.flush();
                std::cerr.flush();
            },
        };
    }

private:
    LogMode previous_;
    std::unique_ptr<LogScope> test_;
};
}

int main(int argc, char** argv) {
    try {
        bool list = false, json = false, verbose = false;
        std::string filter = "*", reportPath;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            auto value = [&]() -> std::string {
                if (++i == argc) throw Error("{} needs a value", arg);
                return argv[i];
            };
            if (arg == "--list") list = true;
            else if (arg == "--json") json = true;
            else if (arg == "-v" || arg == "--verbose") verbose = true;
            else if (arg == "--report") reportPath = value();
            else if (arg == "--help") { Logger::message("<a2>{}</>", Usage); return 0; }
            else if (arg.starts_with('-')) throw Error("unknown option '{}'", arg);
            else filter = arg;
        }
        auto& tests = Lattice::Test::instance();
        RunResult report;
        if (list) {
            report.registered = tests.tests().size();
            for (auto& info : tests.list(filter)) report.tests.push_back({.test = std::move(info)});
        } else if (verbose) {
            Progress progress;
            report = tests.run(filter, progress.callbacks());
        } else report = tests.run(filter);

        if (!reportPath.empty()) {
            std::ofstream out(reportPath, std::ios::binary);
            out << writeReport(report);
            if (!out) throw Error("cannot write test report '{}'", reportPath);
        }
        if (json) std::cout << writeJson(report) << '\n';
        else if (list) {
            for (const auto& test : report.tests)
                Logger::message("<a2><b>{}</></><mut>{}</>", test.test.name, test.test.description.empty() ? "" : "  " + test.test.description);
        } else if (reportPath.empty()) {
            // Under ltest (a report is asked for) the summary is ltest's.
            if (!verbose) failures(report);
            summary(report);
        }
        if (report.tests.empty() && !json) Logger::message("<wrn>No tests match '{}'</>", filter);
        return report.exitCode();
    } catch (const std::exception& error) {
        Logger::message("<err><b>lattice-test:</></><err> {}</>", error.what());
        return 2;
    }
}
