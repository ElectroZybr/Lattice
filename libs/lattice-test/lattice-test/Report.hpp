#pragma once
#include "Tests.hpp"

namespace Lattice::Testing {
/// Versioned TSV — the protocol between a suite and ltest; JSON — for external consumers.
LT_TEST std::string writeReport(const RunResult& result);
LT_TEST RunResult readReport(std::string_view text);
LT_TEST std::string jsonString(std::string_view value);
LT_TEST std::string writeJson(const RunResult& result);
}
