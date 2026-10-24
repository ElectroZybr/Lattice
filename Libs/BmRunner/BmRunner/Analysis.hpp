#pragma once

#include <cstddef>
#include <format>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <BmRunner/BenchTypes.hpp>
#include <BmRunner/BigO.hpp>
#include <BmRunner/Visibility.hpp>


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

/// Значение оси параметров исполнения (`workers`, `batch`) у варианта реализации.
struct ParameterValue {
    std::string name;
    double value = 0;
};

/// Имя варианта: реализация и её параметры — `cpu` (один поток), `cpu×16` (workers),
/// `gpu[batch=4]`.
inline std::string variantLabel(std::string_view implementation, const std::vector<ParameterValue>& params) {
    std::string label(implementation);
    for (const auto& param : params) {
        const auto value = std::format("{}", param.value);
        if (param.name == "workers")
            label += param.value == 1 ? "" : "×" + value;   // one thread: just `cpu`
        else
            label += "[" + param.name + "=" + value + "]";
    }
    return label;
}

/// `workers=16 batch=4`: параметры по имени; пусто — `-`.
inline std::string parametersText(const std::vector<ParameterValue>& params) {
    std::string text;
    for (const auto& param : params)
        text += std::format("{}{}={}", text.empty() ? "" : " ", param.name, param.value);
    return text;
}

struct BenchResult {
    std::string name;    // работа (Walls/SoftForces → SoftForces)
    std::string group;
    std::vector<PointResult> points;
    std::vector<AnalysisResult> analysis;

    // Вариант: реализация работы и значения осей исполнения. Пустая реализация —
    // работа с одной реализацией (`measure`), она не сравнивается.
    std::string implementation;
    std::vector<ParameterValue> params;

    std::string label() const { return variantLabel(implementation, params); }
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
