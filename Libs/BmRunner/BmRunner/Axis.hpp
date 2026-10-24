#pragma once

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <BmRunner/Analysis.hpp>
#include <BmRunner/Visibility.hpp>
#include <Lattice/Tools/Exception.hpp>

namespace Lattice::Benchmarks {

/**
 @file Axis.hpp
 @brief Оси вариантов работы и контекст запуска варианта.

 Ось — тип, объявленный там, где живёт механизм (`Tasks::Workers` рядом с пулом), а не
 строка. BmRunner знает только форму оси, поэтому модуль механизма не зависит от BmRunner:

     struct Workers {
         using Value = size_t;
         static constexpr std::string_view name = "workers";
         static std::vector<size_t> values();          // по умолчанию
         static std::vector<size_t> sweep();           // --sweep workers (необязательно)
         static std::string label(size_t);             // в имени варианта: `×16` (необязательно)
     };

 Значение — число или перечисление; у перечисления нужны `text(v)` и `parse(text)`.
*/

template<typename A>
concept AxisType = requires {
    typename A::Value;
    { A::name } -> std::convertible_to<std::string_view>;
    { A::values() } -> std::convertible_to<std::vector<typename A::Value>>;
} && (std::is_arithmetic_v<typename A::Value> || std::is_enum_v<typename A::Value>);

/// Значение оси у варианта: число для `run.value<A>()`, текст для `params`, подпись для имени.
struct AxisValue {
    std::string name;
    double value = 0;
    std::string text;
    std::string label;
};

/// Значения осей из командной строки: `--sweep <ось>`, `--axis <ось>=v1,v2`.
BMRUNNER_API bool sweeping(std::string_view axis);
BMRUNNER_API const std::vector<std::string>* axisOverride(std::string_view axis);

namespace Axes {

template<AxisType A>
std::string text(typename A::Value value) {
    if constexpr (requires { { A::text(value) } -> std::convertible_to<std::string>; })
        return std::string(A::text(value));
    else {
        static_assert(std::is_arithmetic_v<typename A::Value>, "an enum axis needs text(v) and parse(text)");
        return std::format("{}", value);
    }
}

template<AxisType A>
typename A::Value parse(std::string_view text) {
    using Value = typename A::Value;
    if constexpr (requires { { A::parse(text) } -> std::convertible_to<Value>; })
        return A::parse(text);
    else {
        static_assert(std::is_arithmetic_v<Value>, "an enum axis needs text(v) and parse(text)");
        Value value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size())
            throw Exception<AxisValue>("axis '{}': '{}' is not a value", A::name, text);
        return value;
    }
}

template<AxisType A>
std::string label(typename A::Value value) {
    if constexpr (requires { { A::label(value) } -> std::convertible_to<std::string>; })
        return std::string(A::label(value));
    else
        return std::format("[{}={}]", A::name, text<A>(value));
}

/// Значения оси для запуска: `--axis` → `--sweep` (`sweep()`, иначе `values()`) → `values()`.
template<AxisType A>
std::vector<AxisValue> values() {
    using Value = typename A::Value;
    std::vector<Value> chosen;
    if (const auto* given = axisOverride(A::name))
        for (const auto& text : *given)
            chosen.push_back(parse<A>(text));
    else if constexpr (requires { { A::sweep() } -> std::convertible_to<std::vector<Value>>; })
        chosen = sweeping(A::name) ? std::vector<Value>(A::sweep()) : std::vector<Value>(A::values());
    else
        chosen = A::values();
    if (chosen.empty())
        throw Exception<AxisValue>("axis '{}' has no values", A::name);

    std::vector<AxisValue> result;
    for (const Value value : chosen) {
        if (std::ranges::find(result, text<A>(value), &AxisValue::text) != result.end())
            continue;
        double number;
        if constexpr (std::is_enum_v<Value>)
            number = double(std::to_underlying(value));
        else
            number = double(value);
        result.push_back({std::string(A::name), number, text<A>(value), label<A>(value)});
    }
    return result;
}

}

/// Объявленная ось: её значения вычисляются при запуске (после разбора командной строки).
struct AxisDecl {
    std::string name;
    std::vector<AxisValue> (*values)();
};

template<AxisType A>
AxisDecl axisDecl() {
    return {std::string(A::name), &Axes::values<A>};
}

/// Значения осей в `params`: `fill=0.1 workers=16`.
inline std::vector<ParameterValue> parameters(const std::vector<AxisValue>& values) {
    std::vector<ParameterValue> result;
    for (const AxisValue& value : values)
        result.push_back({value.name, value.value, value.text});
    return result;
}

class Trace;

/// Контекст запуска варианта: размер, значения осей входа и исполнения, разметка фаз.
/// Реализация берёт отсюда всё, что зависит от варианта, — и не знает, как его перебирают.
class Run {
public:
    Run() = default;
    Run(Trace* trace, std::vector<AxisValue> values) : trace_(trace), values_(std::move(values)) {}

    size_t n() const noexcept { return n_; }

    Trace& trace() const {
        if (!trace_)
            throw Exception<Run>("no trace outside a measurement");
        return *trace_;
    }

    template<AxisType A>
    typename A::Value value() const {
        for (const AxisValue& value : values_)
            if (value.name == A::name) {
                if constexpr (std::is_enum_v<typename A::Value>)
                    return static_cast<typename A::Value>(
                        static_cast<std::underlying_type_t<typename A::Value>>(value.value));
                else
                    return static_cast<typename A::Value>(value.value);
            }
        throw Exception<Run>("the variant has no axis '{}': declare it with axis<{}>()", A::name, A::name);
    }

    const std::vector<AxisValue>& values() const noexcept { return values_; }

    void setN(size_t n) noexcept { n_ = n; }

private:
    Trace* trace_ = nullptr;
    std::vector<AxisValue> values_;
    size_t n_ = 0;
};

/// Все сочетания значений осей, последняя ось — быстрее всех. Без осей — одно пустое.
inline std::vector<std::vector<AxisValue>> combinations(const std::vector<AxisDecl>& axes) {
    std::vector<std::vector<AxisValue>> values;
    for (const AxisDecl& axis : axes)
        values.push_back(axis.values());
    std::vector<std::vector<AxisValue>> result;
    std::vector<size_t> index(axes.size(), 0);
    while (true) {
        std::vector<AxisValue> combination;
        for (size_t i = 0; i < axes.size(); ++i)
            combination.push_back(values[i][index[i]]);
        result.push_back(std::move(combination));
        size_t axis = axes.size();
        while (axis > 0 && ++index[axis - 1] == values[axis - 1].size())
            index[--axis] = 0;
        if (axis == 0)
            return result;
    }
}

/// Подписи значений подряд: `×16`, `[fill=0.1]`.
inline std::string labels(const std::vector<AxisValue>& values) {
    std::string result;
    for (const AxisValue& value : values)
        result += value.label;
    return result;
}

}
