#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Lattice::Benchmarks {

/**
 @file Trace.hpp
 @brief Разметка фаз асинхронной работы внутри итерации бенчмарка.

 Любая работа через канал к исполнителю (GPU, удалённый узел, пул потоков, иерархия
 памяти CPU) раскладывается на одни и те же фазы, поэтому исполнения сравнимы по
 столбцам. Фазы хоста меряются часами хоста (`span`), фазы исполнителя приходят готовой
 длительностью (`duration`), часто после итерации (`defer`). Остаток стены итерации
 после фаз хоста — `Unaccounted` (считает capability `Phases`).
*/

enum class Phase : uint8_t {
    Prepare,      // подготовка пакета на хосте: запись команд, сериализация, разбиение
    TransferIn,   // перенос данных к исполнителю
    TransferOut,  // перенос результатов обратно
    Issue,        // передача пакета исполнителю: submit, send, постановка в очередь
    Execute,      // полезная работа
    Complete      // ожидание и синхронизация: wait, ответ, join
};

constexpr std::string_view phaseName(Phase phase) noexcept {
    switch (phase) {
        case Phase::Prepare:     return "Prepare";
        case Phase::TransferIn:  return "Transfer.In";
        case Phase::TransferOut: return "Transfer.Out";
        case Phase::Issue:       return "Issue";
        case Phase::Execute:     return "Execute";
        case Phase::Complete:    return "Complete";
    }
    return "Phase";
}

/// Откуда длительность: часы хоста, исполнитель (timestamps устройства, время сервера)
/// или оценка модели (счётчики × калибровка). Исполнитель и оценка не вычитаются из
/// стены хоста.
enum class TimeSource : uint8_t {
    Host,
    Executor,
    Estimate
};

class Trace {
    using Clock = std::chrono::steady_clock;

public:
    /// Накопленная за семпл фаза: сумма по итерациям.
    struct Entry {
        Phase phase;
        TimeSource source;
        std::string label;
        double nanoseconds = 0;
        uint64_t bytes = 0;
        uint64_t count = 0;
    };

    /// Участок хоста: длительность записывается в деструкторе.
    class [[nodiscard]] Span {
        Trace* trace_;
        size_t entry_;
        Clock::time_point start_;

    public:
        Span(Trace& trace, size_t entry) noexcept
            : trace_(&trace), entry_(entry), start_(Clock::now()) {}

        Span(Span&& other) noexcept
            : trace_(std::exchange(other.trace_, nullptr)), entry_(other.entry_), start_(other.start_) {}

        Span(const Span&) = delete;
        Span& operator=(const Span&) = delete;
        Span& operator=(Span&&) = delete;

        /// Объём, перенесённый в этом участке (для `Transfer`): даёт пропускную способность.
        void bytes(uint64_t value) noexcept {
            if (trace_)
                trace_->entries_[entry_].bytes += value;
        }

        ~Span() {
            if (!trace_)
                return;
            const auto elapsed = Clock::now() - start_;
            Entry& entry = trace_->entries_[entry_];
            entry.nanoseconds += std::chrono::duration<double, std::nano>(elapsed).count();
            ++entry.count;
            ++trace_->spans_;
        }
    };

    /// Участок хоста; метка различает фазы одного вида (`Transfer.In:positions`).
    Span span(Phase phase, std::string_view label = {}) {
        return Span(*this, entry(phase, TimeSource::Host, label));
    }

    /// Длительность, измеренная не часами хоста (или оценка), с объёмом переноса.
    void duration(
        Phase phase,
        double nanoseconds,
        std::string_view label = {},
        uint64_t bytes = 0,
        TimeSource source = TimeSource::Executor
    ) {
        Entry& value = entries_[entry(phase, source, label)];
        value.nanoseconds += nanoseconds;
        value.bytes += bytes;
        ++value.count;
    }

    /// Отчёт, который станет известен позже (resolve запросов устройства, ответ сервера):
    /// выполняется в конце семпла, вне замера. Захватывай по ссылке — тогда вызов не
    /// выделяет память.
    void defer(std::function<void(Trace&)> report) {
        deferred_.push_back(std::move(report));
    }

    /// Интервалы внешнего таймера (timestamps устройства, время сервера, …) как фаза
    /// исполнителя, в конце семпла. Договорённость по форме, без зависимости от таймера:
    /// `timer.read()` возвращает интервалы с полями `name`, `nanoseconds` (сумма), `count`
    /// (измерено) и `skipped` (не измерено — экстраполируется по измеренным). Вызов на
    /// каждой итерации; читает первый отложенный отчёт, остальные получают пустое.
    template<class Timer>
    void deferIntervals(Timer& timer, Phase phase = Phase::Execute, TimeSource source = TimeSource::Executor) {
        defer([&timer, phase, source](Trace& report) {
            for (const auto& interval : timer.read())
                if (interval.count)
                    report.duration(phase,
                        double(interval.nanoseconds) * double(interval.count + interval.skipped) / double(interval.count),
                        interval.name, 0, source);
        });
    }

    std::span<const Entry> entries() const noexcept {
        return entries_;
    }

    /// Число участков хоста за семпл: их собственная цена вычитается из остатка.
    uint64_t spans() const noexcept {
        return spans_;
    }

    /// Начало семпла: значения обнуляются, набор фаз сохраняется.
    void reset() noexcept {
        for (Entry& value : entries_) {
            value.nanoseconds = 0;
            value.bytes = 0;
            value.count = 0;
        }
        spans_ = 0;
        deferred_.clear();
    }

    /// Конец семпла: выполнить отложенные отчёты.
    void flush() {
        for (size_t i = 0; i < deferred_.size(); ++i)
            deferred_[i](*this);
        deferred_.clear();
    }

private:
    size_t entry(Phase phase, TimeSource source, std::string_view label) {
        for (size_t i = 0; i < entries_.size(); ++i) {
            const Entry& value = entries_[i];
            if (value.phase == phase && value.source == source && value.label == label)
                return i;
        }
        entries_.push_back({.phase = phase, .source = source, .label = std::string(label)});
        return entries_.size() - 1;
    }

    std::vector<Entry> entries_;
    std::vector<std::function<void(Trace&)>> deferred_;
    uint64_t spans_ = 0;
};

}
