#include "Report.hpp"
#include <algorithm>
#include <charconv>
#include <sstream>
#include <Lattice/tools/Exception.hpp>
namespace Lattice::Testing {
using Error = Exception<RunResult>;
namespace {
std::string escape(std::string_view input) {
    std::string out;
    for (char c : input) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    return out;
}
std::vector<std::string> fields(std::string_view line) {
    std::vector<std::string> out(1);
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\t') { out.emplace_back(); continue; }
        if (c == '\\') {
            if (++i == line.size()) throw Error("incomplete report escape");
            switch (line[i]) {
                case 't': c = '\t'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                default: throw Error("invalid report escape");
            }
        }
        out.back() += c;
    }
    return out;
}
uint64_t number(std::string_view text) {
    uint64_t value = 0;
    auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (code != std::errc{} || end != text.data() + text.size()) throw Error("invalid report number");
    return value;
}
std::string jsonEscape(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += char(c);
    }
    return out + '"';
}
}
std::string jsonString(std::string_view value) { return jsonEscape(value); }
std::string writeReport(const RunResult& result) {
    std::ostringstream out;
    out << "lattice-test-v1\t" << result.registered << '\t' << result.elapsedNs << '\n';
    for (const auto& test : result.tests) {
        out << "T\t" << escape(test.test.name) << '\t' << escape(test.test.description) << '\t'
            << statusName(test.status) << '\t' << test.elapsedNs << '\t' << escape(test.skipReason) << '\n';
        for (const auto& error : test.diagnostics)
            out << "D\t" << escape(error.kind) << '\t' << escape(error.expression) << '\t'
                << escape(error.file) << '\t' << error.line << '\t' << escape(error.message) << '\n';
    }
    out << "end\t" << result.tests.size() << '\n';
    return out.str();
}
RunResult readReport(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::string line;
    if (!std::getline(in, line)) throw Error("empty test report");
    auto f = fields(line);
    if (f.size() != 3 || f[0] != "lattice-test-v1") throw Error("unsupported test report");
    RunResult report{.registered = number(f[1]), .elapsedNs = number(f[2])};
    bool ended = false;
    while (std::getline(in, line)) {
        if (ended) throw Error("data after report end");
        f = fields(line);
        if (f.size() == 2 && f[0] == "end") {
            if (number(f[1]) != report.tests.size() || report.tests.size() > report.registered)
                throw Error("inconsistent test report counts");
            ended = true;
        } else if (f.size() == 6 && f[0] == "T" && !f[1].empty()) {
            Status status;
            if (f[3] == "passed") status = Status::Passed;
            else if (f[3] == "failed") status = Status::Failed;
            else if (f[3] == "skipped") status = Status::Skipped;
            else if (f[3] == "listed") status = Status::Listed;
            else throw Error("invalid report status");
            if (std::ranges::any_of(report.tests, [&](const auto& t) { return t.test.name == f[1]; }))
                throw Error("duplicate test in report");
            report.tests.push_back({{f[1], f[2]}, status, number(f[4]), {}, f[5]});
        } else if (f.size() == 6 && f[0] == "D" && !report.tests.empty()) {
            auto location = number(f[4]);
            if (location > 2147483647) throw Error("invalid diagnostic line");
            report.tests.back().diagnostics.push_back({f[1], f[2], f[3], f[5], int(location)});
        } else throw Error("invalid test report row");
    }
    if (!ended) throw Error("truncated test report");
    for (const auto& test : report.tests)
        if (test.status != Status::Failed && !test.diagnostics.empty()) throw Error("diagnostics on a successful test");
    return report;
}
std::string writeJson(const RunResult& result) {
    std::ostringstream out;
    out << "{\"schema\":\"lattice-test-v1\",\"registered\":" << result.registered
        << ",\"selected\":" << result.tests.size() << ",\"passed\":" << result.count(Status::Passed)
        << ",\"failed\":" << result.count(Status::Failed) << ",\"skipped\":" << result.count(Status::Skipped)
        << ",\"elapsed_ns\":" << result.elapsedNs << ",\"tests\":[";
    bool comma = false;
    for (const auto& test : result.tests) {
        if (comma) out << ',';
        comma = true;
        out << "{\"name\":" << jsonString(test.test.name) << ",\"description\":" << jsonString(test.test.description)
            << ",\"status\":" << jsonString(statusName(test.status)) << ",\"elapsed_ns\":" << test.elapsedNs
            << ",\"skip_reason\":" << jsonString(test.skipReason) << ",\"diagnostics\":[";
        bool diagnosticComma = false;
        for (const auto& error : test.diagnostics) {
            if (diagnosticComma) out << ',';
            diagnosticComma = true;
            out << "{\"kind\":" << jsonString(error.kind) << ",\"expression\":" << jsonString(error.expression)
                << ",\"file\":" << jsonString(error.file) << ",\"line\":" << error.line << ",\"message\":" << jsonString(error.message) << '}';
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}
}
