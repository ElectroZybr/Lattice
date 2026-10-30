#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <lbm-runner/Stages.hpp>
#include <lbm-runner/Visibility.hpp>

namespace Lattice::Benchmarks {

/// Bytes the heap gained over the measured part of an iteration: the median of the samples.
/// The heap statistics come from the adapter in Metrics/platform (glibc on Linux).
class LBM_API Heap : public MetricCapability<"Heap", Heap> {
    size_t start_ = 0;
    double bytes_ = 0;
    std::vector<double> samples_;

public:
    inline static constexpr auto bytes = defineMetric("bytes", Unit::Bytes, MetricFlags::PerIteration);
    inline static constexpr auto Schema = defineSchema(bytes);

    bool available() const noexcept override { return usedBytes().has_value(); }

    std::string_view unavailableReason() const noexcept override;

    void begin() override { bytes_ = 0; }

    void start() override { start_ = usedBytes().value_or(0); }

    void stop() override { bytes_ += static_cast<double>(usedBytes().value_or(0)) - static_cast<double>(start_); }

    Metrics end() override {
        samples_.push_back(bytes_);
        return {.schema = Schema, .values = {bytes_}};
    }

    Metrics result() override {
        if (samples_.empty()) return {};
        std::ranges::sort(samples_);
        const double median = samples_[samples_.size() / 2];
        samples_.clear();
        return {.schema = Schema, .values = {median}};
    }

private:
    /// Bytes in use by the heap; nullopt when the OS adapter cannot tell.
    static std::optional<size_t> usedBytes() noexcept;
};

}
