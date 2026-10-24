#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <Lattice/Tools/Exception.hpp>

namespace Lbm {

/// lbm.toml in the folder `lbm` runs in — optional: suites are found as xmake targets
/// `<Name>.bench`, and the code of a run is the suite's repository plus the project's
/// local package repositories. The file adds what cannot be found (all keys optional):
///
///     results = "lattice-bmrunner-results"   # results repository
///     code = ["../Other"]                     # more code repositories recorded per run
///
///     [suites]                                # name = binary; replaces the xmake search
///     Boundaries = "build/bench/Boundaries.bench"
///
///     [thresholds]
///     relative = 0.05
///     alpha = 0.01
///     noisy = 0.10
///
/// Only this subset of TOML: tables, strings, numbers, arrays of strings, comments.
struct Config {
    std::filesystem::path folder;              // where lbm.toml is (or the working folder)
    std::filesystem::path results;
    std::vector<std::filesystem::path> code;
    std::map<std::string, std::filesystem::path> suites;
    double relative = 0.05, alpha = 0.01, noisy = 0.10;

    static Config load(const std::filesystem::path& folder);
};

namespace detail {

inline std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

/// Strip a comment that is not inside a string.
inline std::string_view uncomment(std::string_view line) {
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"' && (i == 0 || line[i - 1] != '\\'))
            quoted = !quoted;
        else if (line[i] == '#' && !quoted)
            return line.substr(0, i);
    }
    return line;
}

inline std::string unquote(std::string_view value, size_t line) {
    value = trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"')
        throw Lattice::Exception<Config>("lbm.toml:{}: expected a quoted string, got '{}'", line, value);
    std::string result;
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] == '\\' && i + 2 < value.size())
            ++i;
        result += value[i];
    }
    return result;
}

}

inline Config Config::load(const std::filesystem::path& folder) {
    Config config{.folder = std::filesystem::absolute(folder)};
    config.results = config.folder / "lattice-bmrunner-results";
    const auto path = config.folder / "lbm.toml";
    std::ifstream in(path);
    if (!in)
        return config;

    std::string section;
    std::string text;
    size_t number = 0;
    while (std::getline(in, text)) {
        ++number;
        const std::string_view line = detail::trim(detail::uncomment(text));
        if (line.empty())
            continue;
        if (line.front() == '[') {
            if (line.back() != ']')
                throw Lattice::Exception<Config>("lbm.toml:{}: unclosed table header", number);
            section = std::string(detail::trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string_view::npos)
            throw Lattice::Exception<Config>("lbm.toml:{}: expected 'key = value'", number);
        const std::string key(detail::trim(line.substr(0, equals)));
        const std::string_view value = detail::trim(line.substr(equals + 1));

        auto numberValue = [&] {
            try {
                return std::stod(std::string(value));
            } catch (...) {
                throw Lattice::Exception<Config>("lbm.toml:{}: '{}' is not a number", number, value);
            }
        };
        if (section.empty() && key == "results") {
            config.results = config.folder / detail::unquote(value, number);
        } else if (section.empty() && key == "code") {
            if (value.size() < 2 || value.front() != '[' || value.back() != ']')
                throw Lattice::Exception<Config>("lbm.toml:{}: code is an array of strings", number);
            std::string_view items = value.substr(1, value.size() - 2);
            while (!detail::trim(items).empty()) {
                const size_t comma = items.find(',');
                const auto item = detail::trim(items.substr(0, comma));
                if (!item.empty())
                    config.code.push_back(config.folder / detail::unquote(item, number));
                items = comma == std::string_view::npos ? std::string_view{} : items.substr(comma + 1);
            }
        } else if (section == "suites") {
            config.suites[key] = config.folder / detail::unquote(value, number);
        } else if (section == "thresholds" && key == "relative") {
            config.relative = numberValue();
        } else if (section == "thresholds" && key == "alpha") {
            config.alpha = numberValue();
        } else if (section == "thresholds" && key == "noisy") {
            config.noisy = numberValue();
        } else {
            throw Lattice::Exception<Config>("lbm.toml:{}: unknown key '{}'{}", number, key,
                section.empty() ? "" : " in [" + section + "]");
        }
    }
    return config;
}

}
