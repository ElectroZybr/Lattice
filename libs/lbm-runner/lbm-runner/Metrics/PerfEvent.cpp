#include "PerfEvent.hpp"

#include <utility>

namespace Lattice::Benchmarks {

PerfEvent::PerfEvent(int handle, void* page, std::string reason) noexcept
    : handle_(handle), page_(page), reason_(std::move(reason)) {}

PerfEvent::~PerfEvent() { close(); }

PerfEvent::PerfEvent(PerfEvent&& other) noexcept
    : handle_(std::exchange(other.handle_, -1)), page_(std::exchange(other.page_, nullptr)),
      reason_(std::move(other.reason_)) {}

PerfEvent& PerfEvent::operator=(PerfEvent&& other) noexcept {
    if (this == &other) return *this;
    close();
    handle_ = std::exchange(other.handle_, -1);
    page_ = std::exchange(other.page_, nullptr);
    reason_ = std::move(other.reason_);
    return *this;
}

uint64_t PerfEvent::delta(const Snapshot& begin, const Snapshot& end) {
    const uint64_t value = end.value - begin.value;
    const uint64_t enabled = end.timeEnabled - begin.timeEnabled;
    const uint64_t running = end.timeRunning - begin.timeRunning;

    if (enabled == running) return value;
    if (!running) return 0;

    const long double scaled =
        static_cast<long double>(value) * static_cast<long double>(enabled) / static_cast<long double>(running);
    return static_cast<uint64_t>(scaled);
}

}
