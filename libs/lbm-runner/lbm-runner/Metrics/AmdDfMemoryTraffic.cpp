#include "AmdDfMemoryTraffic.hpp"

#include <cstdint>

namespace Lattice::Benchmarks {

namespace {

static constexpr uint64_t configs[] = {
    0x0000000000003807,
    0x0000000000003847,
    0x0000000000003887,
    0x00000000000038c7,
    0x0000000100003807,
    0x0000000100003847,
    0x0000000100003887,
    0x00000001000038c7
};

}

AmdDfMemoryTraffic::AmdDfMemoryTraffic() {
    for (size_t i = 0; i < CounterCount; ++i) {
        events_[i] = PerfEvent::uncore("amd_df", configs[i]);

        if (!events_[i].available()) {
            unavailableReason_ = events_[i].unavailableReason();
            close();
            return;
        }
    }
}

bool AmdDfMemoryTraffic::available() const noexcept {
    for (const PerfEvent& event : events_)
        if (!event.available())
            return false;

    return true;
}

std::string_view AmdDfMemoryTraffic::unavailableReason() const noexcept {
    return unavailableReason_;
}

void AmdDfMemoryTraffic::begin() {
    if (!available())
        return;

    for (PerfEvent& event : events_)
        event.reset();

    for (size_t i = 0; i < CounterCount; ++i)
        starts_[i] = events_[i].snapshot();

    for (PerfEvent& event : events_)
        event.enable();
}

uint64_t AmdDfMemoryTraffic::end() {
    if (!available())
        return 0;

    uint64_t count = 0;

    for (size_t i = 0; i < CounterCount; ++i) {
        count += PerfEvent::delta(
            starts_[i],
            events_[i].snapshot()
        );
    }

    for (PerfEvent& event : events_)
        event.disable();

    constexpr uint64_t BytesPerCount = 64;

    return count * BytesPerCount;
}

void AmdDfMemoryTraffic::close() {
    for (PerfEvent& event : events_)
        event = {};
}

}
