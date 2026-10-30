#pragma once

#include <lbm-runner/Visibility.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace Lattice::Benchmarks {

/// What a counter counts, independent of the OS API.
enum class HardwareCounter : uint8_t {
    Cycles,
    Instructions,
    CacheReferences,
    CacheMisses,
    Branches,
    BranchMisses
};

enum class CacheCounter : uint8_t {
    L1dReads,
    L1dMisses,
    LlcReads,
    LlcMisses,
    DtlbMisses
};

enum class SoftwareCounter : uint8_t {
    MinorFaults,
    MajorFaults
};

/// A CPU or uncore event counter. The OS part is the adapter in Metrics/platform: Linux
/// perf_event; on other OSes every counter is unavailable with the reason.
class LBM_API PerfEvent {
public:
    struct Snapshot {
        uint64_t value = 0;
        uint64_t timeEnabled = 0;
        uint64_t timeRunning = 0;
    };

private:
    struct Access;

    int handle_ = -1;
    void* page_ = nullptr;
    std::string reason_;

    PerfEvent(int handle, void* page, std::string reason) noexcept;

public:
    PerfEvent() = default;

    ~PerfEvent();

    PerfEvent(const PerfEvent&) = delete;
    PerfEvent& operator=(const PerfEvent&) = delete;

    PerfEvent(PerfEvent&& other) noexcept;
    PerfEvent& operator=(PerfEvent&& other) noexcept;

    /// User mode of this process; in the group of `leader` (counted and enabled with it) when given.
    static PerfEvent hardware(HardwareCounter counter, const PerfEvent* leader = nullptr);

    /// `modelSpecific`: the CPU model's own event instead of the generic one (AMD Zen LLC).
    static PerfEvent cache(CacheCounter counter, bool modelSpecific = false);

    /// User and kernel mode of this process.
    static PerfEvent software(SoftwareCounter counter);

    /// A raw event of the uncore PMU `pmu` (e.g. "amd_df"), system-wide on the PMU's CPU.
    static PerfEvent uncore(std::string_view pmu, uint64_t config);

    bool available() const noexcept {
        return handle_ >= 0;
    }

    /// Why the counter did not open; empty when available.
    const std::string& unavailableReason() const noexcept {
        return reason_;
    }

    bool fastReadable() const noexcept;

    /// `group`: the whole group of this leader.
    void reset(bool group = false);
    void enable(bool group = false);
    void disable(bool group = false);

    // Uses the perf mmap page and RDPMC when the kernel exposes it.
    // Falls back to read(2) on unsupported PMUs/architectures.
    Snapshot snapshot() const;

    // Returns the interval count scaled by time_enabled/time_running.
    static uint64_t delta(
        const Snapshot& begin,
        const Snapshot& end
    );

private:
    void close() noexcept;
};

}
