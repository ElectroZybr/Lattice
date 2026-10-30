#pragma once

// The adapter of this OS: processors(), memory(), gpus(), os(), executable() in
// Lattice::SystemInfo::Platform. The only place with OS macros; included only from Collect.cpp,
// since adapters pull system headers.

#include "../SystemInfo.hpp"

namespace Lattice::SystemInfo::Platform {

/// The architecture this library was built for (a compiler fact, not an OS one).
constexpr CpuArch architecture() {
#if defined(__x86_64__) || defined(_M_X64)
    return CpuArch::X86_64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return CpuArch::AArch64;
#else
    return CpuArch::Unknown;
#endif
}

}

#if defined(__linux__)
#include "Linux.hpp"
#elif defined(_WIN32)
#include "Windows.hpp"
#elif defined(__APPLE__)
#include "Apple.hpp"
#else
#error "lattice-sysinfo: no platform adapter for this OS"
#endif
