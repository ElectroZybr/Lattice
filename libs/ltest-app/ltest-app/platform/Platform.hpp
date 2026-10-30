#pragma once

// The adapter of this OS: run() and programName() in Lattice::Testing::Platform. The only place
// with OS macros; included only from Suites.cpp, since adapters pull system headers.

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "../Suites.hpp"

namespace Lattice::Testing::Platform {

struct Child {
    int exitCode;
    bool timeout;
};

}

#if defined(_WIN32)
#include "Windows.hpp"
#elif defined(__unix__) || defined(__APPLE__)
#include "Posix.hpp"
#else
#error "ltest: no platform adapter for this OS"
#endif
