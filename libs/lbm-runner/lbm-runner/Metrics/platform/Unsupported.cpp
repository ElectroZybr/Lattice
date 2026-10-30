// Windows and macOS: no user-mode API for CPU and uncore counters, no heap statistics here yet;
// every counter and the heap metric are unavailable with the reason.

#include "../Heap.hpp"
#include "../PerfEvent.hpp"

#include <format>
#include <string>

namespace Lattice::Benchmarks {

namespace {

constexpr std::string_view osName() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "this OS";
#endif
}

std::string unsupported() { return std::format("hardware counters are not supported on {}", osName()); }

}

PerfEvent PerfEvent::hardware(HardwareCounter, const PerfEvent*) { return PerfEvent(-1, nullptr, unsupported()); }
PerfEvent PerfEvent::cache(CacheCounter, bool) { return PerfEvent(-1, nullptr, unsupported()); }
PerfEvent PerfEvent::software(SoftwareCounter) { return PerfEvent(-1, nullptr, unsupported()); }
PerfEvent PerfEvent::uncore(std::string_view, uint64_t) { return PerfEvent(-1, nullptr, unsupported()); }

bool PerfEvent::fastReadable() const noexcept { return false; }
void PerfEvent::reset(bool) {}
void PerfEvent::enable(bool) {}
void PerfEvent::disable(bool) {}
PerfEvent::Snapshot PerfEvent::snapshot() const { return {}; }
void PerfEvent::close() noexcept {}

std::optional<size_t> Heap::usedBytes() noexcept { return std::nullopt; }

std::string_view Heap::unavailableReason() const noexcept {
    static const std::string reason = std::format("heap statistics are not supported on {}", osName());
    return reason;
}

}
