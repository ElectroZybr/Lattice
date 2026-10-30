#include "Perf.hpp"

namespace Lattice::Benchmarks {

Perf::Perf() {
    events_[0] = PerfEvent::hardware(HardwareCounter::Cycles);

    if (!events_[0].available()) {
        reason_ = events_[0].unavailableReason();
        return;
    }

    const PerfEvent* leader = &events_[0];

    events_[1] = PerfEvent::hardware(HardwareCounter::Instructions, leader);
    events_[2] = PerfEvent::hardware(HardwareCounter::CacheReferences, leader);
    events_[3] = PerfEvent::hardware(HardwareCounter::CacheMisses, leader);
    events_[4] = PerfEvent::hardware(HardwareCounter::Branches, leader);
    events_[5] = PerfEvent::hardware(HardwareCounter::BranchMisses, leader);

    for (const PerfEvent& event : events_) {
        if (!event.available()) {
            reason_ = event.unavailableReason();
            close();
            return;
        }
    }
}

Perf::Result Perf::readCounters() const {
    if (!available())
        return {};

    auto value = [&](size_t index) {
        return PerfEvent::delta(
            starts_[index],
            events_[index].snapshot()
        );
    };

    return {
        .cycles = value(0),
        .instructions = value(1),
        .cacheReferences = value(2),
        .cacheMisses = value(3),
        .branches = value(4),
        .branchMisses = value(5)
    };
}

void Perf::begin() {
    sample_ = {};

    if (!available())
        return;

    events_[0].reset(true);
    events_[0].enable(true);
}

void Perf::start() {
    if (!available())
        return;

    for (size_t i = 0; i < CounterCount; ++i)
        starts_[i] = events_[i].snapshot();
}

void Perf::stop() {
    if (!available())
        return;

    sample_ += readCounters();
}

Metrics Perf::end() {
    if (!available())
        return {};

    events_[0].disable(true);

    totals_ += sample_;
    ++samples_;

    return makeMetrics(sample_);
}

Metrics Perf::result() {
    if (samples_ == 0)
        return {};

    const double samples = static_cast<double>(samples_);

    Metrics metrics = makeMetrics(totals_);

    for (size_t i = 0; i < metrics.values.size(); ++i)
        if (hasFlag(metrics.schema[i].flags, MetricFlags::PerIteration))
            metrics.values[i] /= samples;

    totals_ = {};
    samples_ = 0;

    return metrics;
}

Metrics Perf::makeMetrics(const Result& value) {
    return {
        .schema = schema(),
        .values = {
            static_cast<double>(value.cycles),
            static_cast<double>(value.instructions),
            static_cast<double>(value.cacheReferences),
            static_cast<double>(value.cacheMisses),
            static_cast<double>(value.branches),
            static_cast<double>(value.branchMisses),
            value.ipc(),
            value.cpi(),
            value.cacheMissRate() * 100.0,
            value.branchMissRate() * 100.0,
            value.instructionsPerBranch(),
            value.mpki()
        }
    };
}

void Perf::close() {
    for (PerfEvent& event : events_)
        event = {};
}

}
