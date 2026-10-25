#pragma once

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include <lbm-runner/Stages.hpp>
#include <lbm-runner/Trace.hpp>

namespace Lattice::Benchmarks {

/**
 Фазы итерации из `Trace`: время каждой фазы на итерацию, пропускная способность фаз с
 объёмом, стена итерации и остаток `Unaccounted` = стена − фазы хоста − цена самих меток.
 Набор фаз становится известен по ходу измерения, поэтому схема строится динамически и
 хранится до конца процесса (на неё ссылаются результаты).
 Bench добавляет capability сам, если функция бенча принимает `Trace&`.
*/
class Phases : public NamedCapability<"Phases"> {
    using Clock = std::chrono::steady_clock;

    enum class Kind : uint8_t { Wall, Unaccounted, Time, Rate };

    struct Column {
        Kind kind;
        size_t entry = 0;
    };

public:
    void bind(Trace& trace) override {
        trace_ = &trace;
    }

    bool available() const noexcept override {
        return trace_ != nullptr;
    }

    std::string_view unavailableReason() const noexcept override {
        return "the bench function does not take a Trace";
    }

    void begin() override {
        wall_ = {};
        if (spanCost_ < 0)
            spanCost_ = measureSpanCost();
    }

    void start() override {
        start_ = Clock::now();
    }

    void stop() override {
        wall_ += Clock::now() - start_;
    }

    Metrics end() override {
        rebuild();
        std::vector<double> values(columns_.size());
        const double wall = std::chrono::duration<double, std::nano>(wall_).count();
        double host = 0;
        for (const Trace::Entry& entry : trace_->entries())
            if (entry.source == TimeSource::Host)
                host += entry.nanoseconds;

        for (size_t i = 0; i < columns_.size(); ++i) {
            const Column& column = columns_[i];
            const auto entries = trace_->entries();
            switch (column.kind) {
                case Kind::Wall:
                    values[i] = wall;
                    break;
                case Kind::Unaccounted:
                    values[i] = std::max(0.0, wall - host - spanCost_ * double(trace_->spans()));
                    break;
                case Kind::Time:
                    values[i] = entries[column.entry].nanoseconds;
                    break;
                case Kind::Rate: {
                    const auto& entry = entries[column.entry];
                    values[i] = entry.nanoseconds > 0 ? double(entry.bytes) * 1e9 / entry.nanoseconds : 0.0;
                    break;
                }
            }
        }

        samples_.resize(columns_.size());
        for (size_t i = 0; i < values.size(); ++i)
            samples_[i].push_back(values[i]);
        return {.schema = schema_, .values = std::move(values)};
    }

    Metrics result() override {
        if (schema_.empty())
            return {};
        std::vector<double> values(columns_.size(), 0.0);
        for (size_t i = 0; i < samples_.size() && i < values.size(); ++i) {
            auto& column = samples_[i];
            if (column.empty())
                continue;
            std::sort(column.begin(), column.end());
            values[i] = column[column.size() / 2];
            column.clear();
        }
        return {.schema = schema_, .values = std::move(values)};
    }

    /// Цена одной метки вне её собственного интервала (вычитается из остатка), нс.
    double spanCost() const noexcept {
        return spanCost_;
    }

private:
    static std::string metricName(const Trace::Entry& entry) {
        std::string name(phaseName(entry.phase));
        if (!entry.label.empty())
            name += ":" + entry.label;
        if (entry.source == TimeSource::Executor)
            name += "@dev";
        else if (entry.source == TimeSource::Estimate)
            name += "~est";
        return name;
    }

    /// Схема меняется, когда появилась фаза или у фазы появился объём. Старые снимки
    /// схемы не трогаются: на них ссылаются уже выданные результаты.
    void rebuild() {
        if (trace_->generation() != builtGeneration_) {
            // Другой вариант: свой набор фаз (старая схема остаётся у выданных результатов).
            columns_.clear();
            descs_.clear();
            schema_ = {};
            builtEntries_ = builtBytes_ = 0;
            builtGeneration_ = trace_->generation();
        }
        const auto entries = trace_->entries();
        size_t withBytes = 0;
        for (const auto& entry : entries)
            withBytes += entry.bytes > 0;
        if (!schema_.empty() && entries.size() == builtEntries_ && withBytes <= builtBytes_)
            return;

        // Колонки сохраняют порядок: новые добавляются в конец, у накопленных семплов
        // индексы не сдвигаются.
        auto has = [&](Kind kind, size_t entry) {
            return std::ranges::any_of(columns_, [&](const Column& c) { return c.kind == kind && c.entry == entry; });
        };
        auto add = [&](Kind kind, size_t entry, std::string name, Unit unit, MetricFlags flags) {
            columns_.push_back({kind, entry});
            descs_.push_back({intern(std::move(name)), unit, flags});
        };
        if (columns_.empty()) {
            add(Kind::Wall, 0, "wall", Unit::Nanoseconds, MetricFlags::PerIteration);
            add(Kind::Unaccounted, 0, "Unaccounted", Unit::Nanoseconds, MetricFlags::PerIteration | MetricFlags::Live);
        }
        for (size_t i = 0; i < entries.size(); ++i) {
            if (!has(Kind::Time, i))
                add(Kind::Time, i, metricName(entries[i]), Unit::Nanoseconds, MetricFlags::PerIteration | MetricFlags::Live);
            if (entries[i].bytes > 0 && !has(Kind::Rate, i))
                add(Kind::Rate, i, metricName(entries[i]) + ".rate", Unit::BytesPerSecond, MetricFlags::Live);
        }
        schema_ = internSchema(descs_);
        builtEntries_ = entries.size();
        builtBytes_ = withBytes;
    }

    // Результаты бенча (BenchResult) живут дольше capability, а схема в них — span.
    // Имена и снимки схем хранятся до конца процесса, одинаковые — один раз.
    static std::string_view intern(std::string name) {
        static std::mutex mutex;
        static std::deque<std::string> names;
        std::lock_guard lock(mutex);
        for (const std::string& known : names)
            if (known == name)
                return known;
        return names.emplace_back(std::move(name));
    }

    static std::span<const MetricDesc> internSchema(const std::vector<MetricDesc>& schema) {
        static std::mutex mutex;
        static std::deque<std::vector<MetricDesc>> schemas;
        std::lock_guard lock(mutex);
        for (const auto& known : schemas)
            if (std::ranges::equal(known, schema, [](const MetricDesc& a, const MetricDesc& b) {
                    return a.name.data() == b.name.data() && a.unit == b.unit && a.flags == b.flags;
                }))
                return known;
        return schemas.emplace_back(schema);
    }

    static double measureSpanCost() {
        constexpr size_t rounds = 16;
        constexpr size_t spans = 1000;
        double best = 1e300;
        Trace scratch;
        for (size_t round = 0; round < rounds; ++round) {
            scratch.reset();
            const auto started = Clock::now();
            for (size_t i = 0; i < spans; ++i)
                auto span = scratch.span(Phase::Execute);
            const double total = std::chrono::duration<double, std::nano>(Clock::now() - started).count();
            best = std::min(best, (total - scratch.entries()[0].nanoseconds) / double(spans));
        }
        return std::max(0.0, best);
    }

    Trace* trace_ = nullptr;
    Clock::time_point start_;
    Clock::duration wall_{};
    double spanCost_ = -1;

    std::vector<Column> columns_;
    std::vector<MetricDesc> descs_;
    std::span<const MetricDesc> schema_;
    size_t builtEntries_ = 0;
    size_t builtBytes_ = 0;
    uint64_t builtGeneration_ = 0;
    std::vector<std::vector<double>> samples_;
};

}
