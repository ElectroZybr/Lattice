#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <lbm-runner/Stages.hpp>
#include <lbm-runner/Metrics/PerfEvent.hpp>

namespace Lattice::Benchmarks {

class MemoryEvent : public MetricCapability<"MemoryEvent", MemoryEvent> {
    static constexpr size_t CounterCount = 2;

    std::array<PerfEvent, CounterCount> events_;
    std::array<PerfEvent::Snapshot, CounterCount> starts_;

    uint64_t sampleMinorFaults_ = 0;
    uint64_t sampleMajorFaults_ = 0;

    uint64_t totalMinorFaults_ = 0;
    uint64_t totalMajorFaults_ = 0;

    size_t samples_ = 0;

public:
    inline static constexpr auto minorFaults = defineMetric(
        "minorFaults",
        Unit::Count,
        MetricFlags::PerIteration | MetricFlags::Live
    );

    inline static constexpr auto majorFaults = defineMetric(
        "majorFaults",
        Unit::Count,
        MetricFlags::PerIteration | MetricFlags::Live
    );

    inline static constexpr auto Schema = defineSchema(
        minorFaults,
        majorFaults
    );

    MemoryEvent()
        : events_{
            PerfEvent::software(SoftwareCounter::MinorFaults),
            PerfEvent::software(SoftwareCounter::MajorFaults)
        } {}

    bool available() const noexcept override {
        return events_[0].available() && events_[1].available();
    }

    std::string_view unavailableReason() const noexcept override {
        return events_[0].available()
            ? events_[1].unavailableReason()
            : events_[0].unavailableReason();
    }

    void begin() override {
        sampleMinorFaults_ = 0;
        sampleMajorFaults_ = 0;

        if (!available())
            return;

        for (auto& event : events_) {
            event.reset();
            event.enable();
        }
    }

    void start() override {
        if (!available())
            return;

        for (size_t i = 0; i < CounterCount; ++i)
            starts_[i] = events_[i].snapshot();
    }

    void stop() override {
        if (!available())
            return;

        const auto minor = events_[0].snapshot();
        const auto major = events_[1].snapshot();

        sampleMinorFaults_ += PerfEvent::delta(starts_[0], minor);
        sampleMajorFaults_ += PerfEvent::delta(starts_[1], major);
    }

    Metrics end() override {
        if (!available())
            return {};

        for (auto& event : events_)
            event.disable();

        totalMinorFaults_ += sampleMinorFaults_;
        totalMajorFaults_ += sampleMajorFaults_;
        ++samples_;

        return {
            .schema = schema(),
            .values = {
                static_cast<double>(sampleMinorFaults_),
                static_cast<double>(sampleMajorFaults_)
            }
        };
    }

    Metrics result() override {
        if (samples_ == 0)
            return {};

        const double divisor = static_cast<double>(samples_);

        Metrics metrics{
            .schema = schema(),
            .values = {
                static_cast<double>(totalMinorFaults_) / divisor,
                static_cast<double>(totalMajorFaults_) / divisor
            }
        };

        totalMinorFaults_ = 0;
        totalMajorFaults_ = 0;
        samples_ = 0;

        return metrics;
    }
};

}