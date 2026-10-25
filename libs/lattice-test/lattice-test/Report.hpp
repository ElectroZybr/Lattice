#pragma once
#include "Tests.hpp"

namespace Lattice::Testing {
/// Versioned TSV — the protocol between a suite and ltest; JSON — for external consumers.
LATTICE_TEST_API std::string writeReport(const RunResult& result);
LATTICE_TEST_API RunResult readReport(std::string_view text);
LATTICE_TEST_API std::string jsonString(std::string_view value);
LATTICE_TEST_API std::string writeJson(const RunResult& result);
}
