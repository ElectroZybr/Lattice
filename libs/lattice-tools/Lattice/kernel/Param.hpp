#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Lattice/kernel/Value.hpp>

namespace Lattice {

enum class ParamError : uint8_t {
    Type,    // the value does not convert to the param's type
    Range,   // outside [min, max]
    Choice,  // not one of the choices
    Length,  // longer than maxLength
    Invalid  // refused by the param's own setter
};

/// Why a write was refused: tools show it the same way (CLI, GUI, configuration).
struct ParamRejection {
    ParamError code = ParamError::Invalid;
    std::string message;
    /// The part of a string input at fault, [begin, end); empty — the whole value.
    size_t begin = 0;
    size_t end = 0;
    /// What would have been accepted there (names, tokens); the rules are in ParamRules.
    std::vector<std::string> expected;
};

/// Outcome of a param write. One pointer, and nothing is built when the write is accepted.
class [[nodiscard]] ParamResult {
    std::unique_ptr<ParamRejection> rejection_;

public:
    ParamResult() = default;
    ParamResult(ParamRejection rejection)
        : rejection_(std::make_unique<ParamRejection>(std::move(rejection))) {}

    bool ok() const noexcept { return !rejection_; }
    explicit operator bool() const noexcept { return ok(); }
    /// Only for a refused write.
    const ParamRejection& rejection() const noexcept { return *rejection_; }
};

inline ParamRejection reject(
    ParamError code,
    std::string message,
    size_t begin = 0,
    size_t end = 0,
    std::vector<std::string> expected = {}
) {
    return {code, std::move(message), begin, end, std::move(expected)};
}

/// Rules of a param as tools present them (help, GUI); built on request.
struct ParamRules {
    std::optional<double> min;
    std::optional<double> max;
    std::vector<Value> choices;
    std::optional<size_t> maxLength;
    /// The param has its own setter: it may refuse values the rules allow.
    bool setter = false;
};

}
