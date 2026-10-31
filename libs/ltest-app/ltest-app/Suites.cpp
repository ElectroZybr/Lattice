#include "Suites.hpp"

#include <lattice-test/Report.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <thread>
#include <format>
#include <random>
#include <lattice-tools/Exception.hpp>

#include "platform/Platform.hpp"

namespace Lattice::Testing {
using Error = Exception<Suite>;
namespace fs = std::filesystem;
namespace {
std::string read(const fs::path& path, size_t limit = 16 * 1024 * 1024) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("cannot read '{}'", path.string());
    std::string out;
    char buffer[8192];
    while (in.read(buffer, sizeof buffer) || in.gcount()) {
        if (out.size() + size_t(in.gcount()) > limit) throw Error("report too large: '{}'", path.string());
        out.append(buffer, size_t(in.gcount()));
    }
    if (!in.eof()) throw Error("cannot read '{}'", path.string());
    return out;
}
/// A new directory only this user reads: run-<random> in `parent`.
fs::path workspace(const fs::path& parent) {
    fs::create_directories(parent);
    std::random_device random;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto path = fs::absolute(parent) / std::format("run-{:08x}", random());
        if (fs::create_directory(path)) {
            fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace);
            return path;
        }
    }
    throw Error("cannot create test workspace in '{}'", parent.string());
}
struct Temp {
    fs::path path;
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};
using Process = Platform::Child;
Process process(const std::string& binary, const std::vector<std::string>& arguments,
                const fs::path& directory, const fs::path& log, std::chrono::milliseconds timeout,
                const std::function<void(std::string_view)>& output = {}) {
    if (timeout.count() <= 0) throw Error("timeout must be positive");
    // Reopened on every poll: libc++ reads through FILE*, whose end of file stays set on macOS.
    std::streamoff offset = 0;
    auto forward = [&] {
        if (!output) return;
        std::ifstream live(log, std::ios::binary);
        if (!live.is_open() || !live.seekg(offset)) return;
        char buffer[8192];
        while (live.read(buffer, sizeof buffer) || live.gcount()) {
            offset += live.gcount();
            output(std::string_view(buffer, size_t(live.gcount())));
        }
    };
    return Platform::run(binary, arguments, directory, log, timeout, forward);
}
std::string trim(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    return text;
}
/// The last lines of a process log: enough to see a loader error or a crash.
std::string tail(const fs::path& path, size_t lines = 20) {
    std::error_code error;
    if (!fs::exists(path, error)) return {};
    const auto size = fs::file_size(path, error);
    std::ifstream in(path, std::ios::binary);
    if (error || !in) return {};
    const auto from = size > 8192 ? size - 8192 : 0;
    in.seekg(std::streamoff(from));
    std::string text{std::istreambuf_iterator<char>(in), {}};
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    size_t at = text.size();
    for (size_t found = 0; found < lines; ++found) {
        at = text.rfind('\n', at == 0 ? 0 : at - 1);
        if (at == std::string::npos) return text;
    }
    return text.substr(at + 1);
}

std::string command(const std::vector<std::string>& arguments, const fs::path& directory) {
    Temp temporary{workspace(fs::temp_directory_path())};
    const auto log = temporary.path / "xmake.log";
    const auto result = process("xmake", arguments, directory, log, std::chrono::milliseconds(120000));
    if (result.exitCode) throw Error("xmake failed (exit {}); {}", result.exitCode, read(log, 1024 * 1024).substr(0, 2000));
    return plain(read(log, 1024 * 1024));
}

}
std::vector<Suite> discover(const fs::path& directory) {
    const auto root = fs::canonical(directory);
    std::vector<Suite> suites;
    if (fs::is_regular_file(root / "xmake.lua")) {
        std::istringstream listed(command({"show", "-y", "-l", "targets"}, root));
        std::string target;
        while (listed >> target) {
            if (!target.ends_with(".tests")) continue;
            const auto shown = command({"show", "-y", "-t", target}, root);
            if (shown.find("kind: binary") == std::string::npos) continue;
            const auto at = shown.find("targetfile:");
            if (at == std::string::npos) throw Error("xmake target {} has no targetfile", target);
            const auto path = trim(shown.substr(at + 11, shown.find('\n', at) - at - 11));
            suites.push_back({target.substr(0, target.size() - 6), fs::absolute(root / path), root, target});
        }
    } else {
        for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
            const auto& entry = *it;
            const auto name = entry.path().filename().string();
            if (entry.is_directory() && (name.starts_with('.') || name == "node_modules")) { it.disable_recursion_pending(); continue; }
            if (!entry.is_regular_file()) continue;
            const auto program = Platform::programName(entry.path());
            if (!program || !program->ends_with(".tests")) continue;
            auto relative = (entry.path().parent_path() / *program).lexically_relative(root).generic_string();
            suites.push_back({relative.substr(0, relative.size() - 6), entry.path(), root, {}});
        }
    }
    std::ranges::sort(suites, {}, &Suite::name);
    return suites;
}
void buildSuite(const Suite& suite, std::chrono::milliseconds timeout) {
    if (suite.target.empty()) throw Error("suite '{}' has no build target", suite.name);
    Temp temporary{workspace(fs::temp_directory_path())};
    const auto log = temporary.path / "build.log";
    auto result = process("xmake", {"build", "-y", suite.target}, suite.directory, log, timeout);
    if (result.exitCode) throw Error("build failed for '{}': {}", suite.name, read(log, 1024 * 1024).substr(0, 2000));
}
SuiteResult runSuite(const Suite& suite, const SuiteOptions& options) {
    SuiteResult result{.suite = suite};
    Temp temporary;
    fs::path directory, log;
    try {
        if (options.logs.empty()) temporary.path = directory = workspace(fs::temp_directory_path());
        else directory = workspace(options.logs);
        log = directory / "process.log";
        if (!options.logs.empty()) result.log = log;
        const auto path = directory / "report.tsv";
        std::vector<std::string> args{options.filter, "--report", path.string()};
        if (options.verbose) args.push_back("--verbose");
        if (options.list) args.push_back("--list");
        const auto child = process(fs::absolute(suite.binary).string(), args, fs::absolute(suite.directory), log, options.timeout, options.output);
        result.exitCode = child.exitCode;
        if (child.timeout) throw Error("timeout after {} ms", options.timeout.count());
        if (!fs::exists(path)) throw Error("exited {} without a report", child.exitCode);
        result.report = readReport(read(path));
        if (child.exitCode != result.report.exitCode()) throw Error("process exit {} disagrees with report exit {}", child.exitCode, result.report.exitCode());
        for (const auto& test : result.report.tests) {
            if (!matches(options.filter, test.test.name)) throw Error("suite ignored filter");
            if ((test.status == Status::Listed) != options.list) throw Error("suite ignored listing mode");
        }
    } catch (const std::exception& error) {
        result.error = error.what();
        if (result.exitCode == 0) result.exitCode = 2;
        result.output = tail(log);
    }
    return result;
}

std::string suitesJson(const std::vector<SuiteResult>& results) {
    std::ostringstream out;
    size_t passed = 0, failed = 0, skipped = 0, errors = 0, selected = 0;
    for (const auto& result : results) {
        passed += result.report.count(Status::Passed);
        failed += result.report.count(Status::Failed);
        skipped += result.report.count(Status::Skipped);
        selected += result.report.tests.size();
        errors += !result.error.empty();
    }
    out << "{\"schema\":\"ltest-v1\",\"selected\":" << selected << ",\"passed\":" << passed
        << ",\"failed\":" << failed << ",\"skipped\":" << skipped << ",\"errors\":" << errors << ",\"suites\":[";
    bool comma = false;
    for (const auto& result : results) {
        if (comma) out << ',';
        comma = true;
        out << "{\"name\":" << jsonString(result.suite.name) << ",\"exit_code\":" << result.exitCode
            << ",\"binary\":" << jsonString(result.suite.binary.string())
            << ",\"directory\":" << jsonString(result.suite.directory.string())
            << ",\"error\":" << jsonString(result.error) << ",\"output\":" << jsonString(plain(result.output))
            << ",\"log\":" << jsonString(result.log.string())
            << ",\"report\":" << writeJson(result.report) << '}';
    }
    return out.str() + "]}";
}
std::string plain(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && !(text[i] >= '@' && text[i] <= '~')) ++i;
        } else out += text[i];
    }
    return out;
}
}
