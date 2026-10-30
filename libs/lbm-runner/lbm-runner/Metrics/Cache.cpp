#include "Cache.hpp"

#include <utility>



namespace Lattice::Benchmarks {

Cache::Result& Cache::Result::operator+=(const Result& other) noexcept {
    instructions += other.instructions;
    l1dReads += other.l1dReads;
    l1dMisses += other.l1dMisses;
    llcReads += other.llcReads;
    llcMisses += other.llcMisses;
    dtlbMisses += other.dtlbMisses;
    return *this;
}

double Cache::Result::l1dMissRate() const noexcept {
    return l1dReads
        ? static_cast<double>(l1dMisses) / static_cast<double>(l1dReads)
        : 0.0;
}

double Cache::Result::llcMissRate() const noexcept {
    return llcReads
        ? static_cast<double>(llcMisses) / static_cast<double>(llcReads)
        : 0.0;
}

double Cache::Result::mpki(uint64_t misses) const noexcept {
    return Cache::missesPerKiloInstructions(misses, instructions);
}

Cache::Cache() {
    events_[Instructions] = PerfEvent::hardware(HardwareCounter::Instructions);
    events_[L1dReads] = PerfEvent::cache(CacheCounter::L1dReads);
    events_[L1dMisses] = PerfEvent::cache(CacheCounter::L1dMisses);
    events_[LlcReads] = PerfEvent::cache(CacheCounter::LlcReads);
    events_[LlcMisses] = PerfEvent::cache(CacheCounter::LlcMisses);
    events_[DtlbMisses] = PerfEvent::cache(CacheCounter::DtlbMisses);

    if (
        !events_[LlcReads].available() ||
        !events_[LlcMisses].available()
    ) {
        PerfEvent reads = PerfEvent::cache(CacheCounter::LlcReads, true);
        PerfEvent misses = PerfEvent::cache(CacheCounter::LlcMisses, true);

        if (reads.available() && misses.available()) {
            events_[LlcReads] = std::move(reads);
            events_[LlcMisses] = std::move(misses);
        }
    }

    for (const PerfEvent& event : events_) {
        if (!event.available()) {
            reason_ = event.unavailableReason();
            close();
            break;
        }
    }
}

bool Cache::available() const noexcept {
    for (const PerfEvent& event : events_)
        if (!event.available())
            return false;

    return true;
}

void Cache::begin() {
    sample_ = {};

    if (!available())
        return;

    for (PerfEvent& event : events_) {
        event.reset();
        event.enable();
    }
}

void Cache::start() {
    if (!available())
        return;

    for (size_t i = 0; i < CounterCount; ++i)
        starts_[i] = events_[i].snapshot();
}

void Cache::stop() {
    if (!available())
        return;

    sample_ += readCounters();
}

Metrics Cache::end() {
    if (!available())
        return {};

    for (PerfEvent& event : events_)
        event.disable();

    totals_ += sample_;
    ++samples_;

    return makeMetrics(sample_);
}

Metrics Cache::result() {
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

Cache::Result Cache::readCounters() const {
    auto value = [&](size_t index) {
        return PerfEvent::delta(
            starts_[index],
            events_[index].snapshot()
        );
    };

    return {
        .instructions = value(Instructions),
        .l1dReads = value(L1dReads),
        .l1dMisses = value(L1dMisses),
        .llcReads = value(LlcReads),
        .llcMisses = value(LlcMisses),
        .dtlbMisses = value(DtlbMisses)
    };
}

Metrics Cache::makeMetrics(const Result& result) {
    return {
        .schema = schema(),
        .values = {
            static_cast<double>(result.l1dReads),
            static_cast<double>(result.l1dMisses),
            result.l1dMissRate() * 100.0,
            result.mpki(result.l1dMisses),
            static_cast<double>(result.llcReads),
            static_cast<double>(result.llcMisses),
            result.llcMissRate() * 100.0,
            result.mpki(result.llcMisses),
            static_cast<double>(result.dtlbMisses),
            result.mpki(result.dtlbMisses)
        }
    };
}

void Cache::close() {
    for (PerfEvent& event : events_)
        event = {};
}

}
