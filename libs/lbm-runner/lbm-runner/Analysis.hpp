#pragma once

#include <cstddef>
#include <format>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <lbm-runner/BenchTypes.hpp>
#include <lbm-runner/BigO.hpp>
#include <lbm-runner/Visibility.hpp>


namespace Lattice::Benchmarks {

enum class Parameter : uint8_t {
    N
};

inline constexpr Parameter N = Parameter::N;

constexpr ValueRef valueRef(Parameter parameter) {
    return {
        .source = ValueSource::Parameter,
        .capability = {},
        .name = "N",
        .unit = Unit::None,
        .index = static_cast<size_t>(parameter)
    };
}

constexpr ValueRef valueRef(ValueRef ref) noexcept {
    return ref;
}

enum class AnalysisType : uint8_t {
    Growth,
    Correlation,
    Affine,   // y = fixed + per·x: задержка и цена на элемент (относительные МНК)
    Phases    // Affine для каждой фазы Phases по x (раскрывается при run)
};

struct AnalysisRequest {
    AnalysisType type;
    ValueRef x;
    ValueRef y;
};

using AnalysisValueData = std::variant<double, uint64_t, std::string>;

struct AnalysisValue {
    std::string name;
    AnalysisValueData value;
    Unit unit = Unit::None;
};

struct AnalysisResult {
    AnalysisType type;
    ValueRef x;
    ValueRef y;
    std::vector<AnalysisValue> values;

    const AnalysisValue* find(std::string_view name) const noexcept {
        for (const AnalysisValue& value : values)
            if (value.name == name)
                return &value;

        return nullptr;
    }
};

/// Значение оси у варианта: `workers=16`, `fill=0.1`.
struct ParameterValue {
    std::string name;
    double value = 0;
    std::string text;   // как в `params`
};

/// `fill=0.1 workers=16`: параметры по имени; пусто — пустая строка.
inline std::string parametersText(const std::vector<ParameterValue>& params) {
    std::string text;
    for (const auto& param : params)
        text += std::format("{}{}={}", text.empty() ? "" : " ", param.name, param.text);
    return text;
}

/// Значения осей входа в имени работы: `[fill=0.1]`. Другие данные — другая таблица
/// работы, поэтому они в имени бенча, а не в имени варианта.
inline std::string inputLabel(const std::vector<ParameterValue>& inputs) {
    std::string text;
    for (const auto& input : inputs)
        text += std::format("[{}={}]", input.name, input.text);
    return text;
}

struct BenchResult {
    std::string name;    // работа (Walls/SoftForces → SoftForces)
    std::string group;
    std::vector<PointResult> points;
    std::vector<AnalysisResult> analysis;

    // Вариант: реализация работы, значения осей входа и исполнения, имя варианта
    // (`cpu×16`: реализация и подписи осей исполнения). Пустая реализация — бенч с одной
    // реализацией (`measure`), он не сравнивается.
    std::string implementation;
    std::vector<ParameterValue> inputs;
    std::vector<ParameterValue> params;
    std::string variant;

    std::string label() const { return variant.empty() ? implementation : variant; }
};

/// Подгонка y = fixed + per·x по точкам (относительные МНК: вес 1/y²).
struct AffineFit {
    double fixed = 0;
    double per = 0;
    double error = 0;   // RMS относительной ошибки
    bool valid = false;
};

BMRUNNER_API AffineFit fitAffine(std::span<const ComplexityPoint> points);

class BMRUNNER_API Analysis {
    std::vector<AnalysisRequest> requests_;

public:
    template<typename X, typename Y>
    Analysis& growth(X x, Y y) {
        requests_.push_back({
            .type = AnalysisType::Growth,
            .x = valueRef(x),
            .y = valueRef(y)
        });

        return *this;
    }

    /// Задержка и цена на элемент: y = fixed + per·x.
    template<typename X, typename Y>
    Analysis& affine(X x, Y y) {
        requests_.push_back({
            .type = AnalysisType::Affine,
            .x = valueRef(x),
            .y = valueRef(y)
        });

        return *this;
    }

    /// Affine для каждой фазы (capability Phases) по x.
    template<typename X>
    Analysis& phases(X x) {
        for (const AnalysisRequest& request : requests_)
            if (request.type == AnalysisType::Phases)
                return *this;
        requests_.push_back({
            .type = AnalysisType::Phases,
            .x = valueRef(x),
            .y = {.source = ValueSource::Metric, .capability = "Phases"}
        });

        return *this;
    }

    template<typename X, typename Y>
    Analysis& correlation(X x, Y y) {
        requests_.push_back({
            .type = AnalysisType::Correlation,
            .x = valueRef(x),
            .y = valueRef(y)
        });

        return *this;
    }

    std::vector<AnalysisResult> run(std::span<const PointResult> points) const;
    void clear() { requests_.clear(); }

private:
    static double resolve(const PointResult& point, ValueRef ref);
    static Unit resolveUnit(
        std::span<const PointResult> points,
        ValueRef ref
    );
    static std::vector<AnalysisValue> analyzeGrowth(
        std::span<const PointResult> points,
        const AnalysisRequest& request
    );
    static std::vector<AnalysisValue> analyzeAffine(
        std::span<const PointResult> points,
        const AnalysisRequest& request
    );
    static std::vector<AnalysisValue> analyzeCorrelation(
        std::span<const PointResult> points,
        const AnalysisRequest& request
    );
};

}
