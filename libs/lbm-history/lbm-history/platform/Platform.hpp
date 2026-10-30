#pragma once

// The adapter of this OS: runProgram(), runAttached(), interactiveTerminal() and systemHostName()
// in Lattice::BmHistory::Platform. The only place with OS macros; included only from the
// library's .cpp files, since adapters pull system headers.

#include "../Process.hpp"

#if defined(_WIN32)
#include "Windows.hpp"
#elif defined(__unix__) || defined(__APPLE__)
#include "Posix.hpp"
#else
#error "lbm-history: no platform adapter for this OS"
#endif
