// ltest: finds test executables, runs each in its own process and sums their reports.
#include <charconv>
#include <iostream>

#include <lattice-logger/Logger.hpp>
#include <lattice-tools/Exception.hpp>

#include "ltest-app/Suites.hpp"

namespace T = Lattice::Testing;
namespace {
struct Tool;
using Error = Lattice::Exception<Tool>;

constexpr std::string_view Usage = R"(ltest list [suite-glob] [test-glob] [options]
ltest run  [suite-glob] [test-glob] [options]

  --dir <directory>   project or directory with *.tests (default: current)
  --details           every test result
  -v, --verbose       live progress and output of every test
  --json              results as JSON (schema ltest-v1)
  --build             xmake build -y the matching suites first
  --timeout <seconds> per executable (default: 30)
  --logs <directory>  keep process logs and reports (default: temporary, removed)

Without a command, run. Globs take * and ?, quote them.
Exit: 0 passed or listed, 1 failed tests, 2 nothing matched or a suite could not run.)";

void diagnostics(const T::TestResult& test) {
    for (const auto& error : test.diagnostics)
        Logger::message("  <err><b>{}</></><mut>: </><err>{}</><mut>{}</><a2>{}</><mut>{}</><err>{}</>",
                        error.kind, error.expression, error.file.empty() ? "" : " (",
                        error.file.empty() ? "" : error.file + ":" + std::to_string(error.line),
                        error.file.empty() ? "" : ")", error.message.empty() ? "" : ": " + error.message);
}

void result(std::string_view suite, const T::TestResult& test) {
    switch (test.status) {
        case T::Status::Failed: Logger::message("<err><b>failed {}/{}</></>", suite, test.test.name); break;
        case T::Status::Skipped: Logger::message("<wrn><b>skipped {}/{}</></><wrn>: {}</>", suite, test.test.name, test.skipReason); break;
        case T::Status::Passed: Logger::message("<ok><b>passed {}/{}</></>", suite, test.test.name); break;
        case T::Status::Listed: Logger::message("<mut>{}/</><a2>{}</>", suite, test.test.name); break;
    }
    diagnostics(test);
}

void broken(const T::SuiteResult& suite) {
    Logger::message("<err><b>error {}</></><err>: {}</>", suite.suite.name, suite.error);
    if (!suite.output.empty()) Logger::message("<mut>{}</>", suite.output);
    if (!suite.log.empty()) Logger::message("<mut>  log: </><a2>{}</>", suite.log.string());
}

void summary(const std::vector<T::SuiteResult>& suites) {
    size_t passed = 0, failed = 0, skipped = 0, selected = 0, errors = 0;
    for (const auto& suite : suites) {
        passed += suite.report.count(T::Status::Passed);
        failed += suite.report.count(T::Status::Failed);
        skipped += suite.report.count(T::Status::Skipped);
        selected += suite.report.tests.size();
        errors += !suite.error.empty();
    }
    if (failed) {
        Logger::message("<err><b>Failed tests:</></>");
        for (const auto& suite : suites)
            for (const auto& test : suite.report.tests)
                if (test.status == T::Status::Failed) result(suite.suite.name, test);
    }
    Logger::message("<ok><b>{} passed</></><mut>, </><err><b>{} failed</></><mut>, </><wrn>{} skipped</><mut>; {} tests in {} suites</>{}",
                    passed, failed, skipped, selected, suites.size(),
                    errors ? Lattice::TextFormatter::format("<mut>; </><err><b>{} could not run</></>", errors).render() : "");
}
}

int main(int argc, char** argv) {
    try {
        int first = 1;
        std::string command = "run";
        if (argc > 1 && (std::string_view(argv[1]) == "list" || std::string_view(argv[1]) == "run")) command = argv[first++];
        T::SuiteOptions options;
        std::filesystem::path directory = std::filesystem::current_path();
        bool details = false, json = false, build = false;
        std::vector<std::string> globs;
        for (int i = first; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (++i == argc) throw Error("{} needs a value", arg);
                return argv[i];
            };
            if (arg == "--help" || arg == "-h") { Logger::message("{}", Usage); return 0; }
            else if (arg == "--dir") directory = value();
            else if (arg == "--logs") options.logs = std::filesystem::absolute(value());
            else if (arg == "--json") json = true;
            else if (arg == "--details") details = true;
            else if (arg == "-v" || arg == "--verbose") options.verbose = true;
            else if (arg == "--build") build = true;
            else if (arg == "--timeout") {
                const auto text = value();
                int seconds = 0;
                auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), seconds);
                if (code != std::errc{} || end != text.data() + text.size() || seconds < 1 || seconds > 86400)
                    throw Error("timeout must be 1..86400 seconds");
                options.timeout = std::chrono::seconds(seconds);
            } else if (arg.starts_with('-')) throw Error("unknown option '{}'", arg);
            else globs.push_back(arg);
        }
        if (globs.size() > 2) throw Error("expected at most a suite glob and a test glob");
        const std::string suiteGlob = globs.empty() ? "*" : globs[0];
        if (globs.size() == 2) options.filter = globs[1];
        options.list = command == "list";
        if (options.verbose) options.output = [](std::string_view text) {
            std::cerr.write(text.data(), static_cast<std::streamsize>(text.size()));
            std::cerr.flush();
        };

        const auto suites = T::discover(directory);
        if (suites.empty()) {
            if (json) std::cout << T::suitesJson({}) << '\n';
            Logger::message("<wrn>No test suites in '{}'</>", directory.string());
            if (std::filesystem::is_regular_file(directory / "xmake.lua"))
                Logger::message("<wrn>The xmake project declares no binary *.tests targets (Lattice: xmake f -y --lattice_build_extras=y).</>");
            return 2;
        }
        // `ltest list` alone: suite names, nothing is started.
        if (options.list && globs.empty()) {
            std::vector<T::SuiteResult> catalog;
            for (const auto& suite : suites) {
                if (!json) Logger::message("<a2><b>{}</></>", suite.name);
                catalog.push_back({.suite = suite, .exitCode = 0});
            }
            if (json) std::cout << T::suitesJson(catalog) << '\n';
            return 0;
        }

        std::vector<T::SuiteResult> results;
        size_t failed = 0, selected = 0, errors = 0;
        for (const auto& suite : suites) {
            if (!T::matches(suiteGlob, suite.name)) continue;
            if (build) T::buildSuite(suite);
            auto run = T::runSuite(suite, options);
            failed += run.report.count(T::Status::Failed);
            selected += run.report.tests.size();
            errors += !run.error.empty();
            if (!json) {
                if (!run.error.empty()) broken(run);
                else for (const auto& test : run.report.tests)
                    if (options.list || test.status == T::Status::Skipped || (details && !options.verbose)) result(suite.name, test);
            }
            results.push_back(std::move(run));
        }
        if (json) std::cout << T::suitesJson(results) << '\n';
        else if (options.list) Logger::message("<mut>{} tests in {} suites</>", selected, results.size());
        else summary(results);
        if (errors) return 2;
        if (!selected) {
            if (!json) Logger::message("<wrn>No tests match '{}' '{}'</>", suiteGlob, options.filter);
            return 2;
        }
        return failed ? 1 : 0;
    } catch (const std::exception& error) {
        Logger::message("<err><b>ltest:</></><err> {}</>", error.what());
        return 2;
    }
}
