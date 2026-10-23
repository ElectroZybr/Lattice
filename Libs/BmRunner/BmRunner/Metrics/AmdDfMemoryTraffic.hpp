#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <BmRunner/Metrics/MemoryTraffic.hpp>
#include <BmRunner/Metrics/PerfEvent.hpp>
#include <BmRunner/Visibility.hpp>

namespace Lattice::Benchmarks {

class BMRUNNER_API AmdDfMemoryTraffic : public MemoryTrafficBackend {
    static constexpr size_t CounterCount = 8;

    std::array<PerfEvent, CounterCount> events_;
    std::array<PerfEvent::Snapshot, CounterCount> starts_;
    std::string unavailableReason_;

public:
    AmdDfMemoryTraffic();
    ~AmdDfMemoryTraffic() override = default;

    AmdDfMemoryTraffic(const AmdDfMemoryTraffic&) = delete;
    AmdDfMemoryTraffic& operator=(const AmdDfMemoryTraffic&) = delete;

    bool available() const noexcept override;
    std::string_view unavailableReason() const noexcept override;

    void begin() override;
    uint64_t end() override;

private:
    static PerfEvent openCounter(
        uint32_t type,
        int cpu,
        uint64_t config
    );
    void close();
};

}
