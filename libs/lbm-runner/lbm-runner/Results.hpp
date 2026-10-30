#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <lbm-runner/Analysis.hpp>
#include <lbm-runner/BenchTypes.hpp>
#include <lbm-runner/Visibility.hpp>

namespace Lattice::Benchmarks {

/**
 @file Results.hpp
 @brief Результаты запуска как таблица: строка на значение метрики, формат TSV.

 Длинный вид: `group bench implementation params variant n stage metric unit value samples
 measured`. Новая метрика, фаза или стадия — новые строки, а не новая схема; построчный
 diff показывает, какое значение какого бенча изменилось. Бенч — работа со значениями осей
 входа (`Constrain[fill=0.1]`: другие данные — другая таблица); реализация и значения осей
 исполнения (`params`: `workers=16`) — вариант этой работы, `variant` — его имя (`cpu×16`),
 собранное BmRunner по подписям осей. Анализ — строки со стадией
 `analysis`, сводка вариантов — `compare`; у них `n` = `-`. `unit` — единица значения
 (`ns`, `B/s`, `%`…): потребителям не нужно угадывать её по имени метрики.
*/

struct ResultRow {
    std::string group;
    std::string bench;            // работа и оси входа: `Constrain[fill=0.1]`
    std::string implementation;   // `-` у работы с одной реализацией
    std::string params;           // оси исполнения: `workers=16 batch=4`, `-` без осей
    std::string variant;          // `cpu×16`; `-` у работы с одной реализацией
    std::string n;                // число или `-`
    std::string stage;      // имя стадии, `analysis`, `compare`
    std::string metric;     // `Capability.name`, у анализа `y(x).value`
    std::string unit;       // `ns`, `B`, `B/s`, `%`, `ratio`, `cycles`, `count`, `-`
    double value = 0;
    std::vector<double> samples;   // по семплам (на итерацию); пусто — `-`
    std::string measured;   // номер запуска, в котором строка измерена; `-` до сохранения
};

struct ResultTable {
    /// Метаданные запуска: строки `# key: value` перед заголовком.
    std::vector<std::pair<std::string, std::string>> meta;
    std::vector<ResultRow> rows;

    const std::string* find(std::string_view key) const noexcept {
        for (const auto& [name, value] : meta)
            if (name == key)
                return &value;
        return nullptr;
    }
};

/// Строки результата бенча: метрики стадий с семплами, ряды семплов без итоговой
/// метрики (медиана как значение), числовые значения анализа.
LBM_API std::vector<ResultRow> resultRows(const BenchResult& result);
/// Строки сводки вариантов работы: подгонка каждого и пороги `crossover:<slower>`.
LBM_API std::vector<ResultRow> resultRows(const Comparison& comparison);

/// Единица в TSV: `ns`, `B/s`…; `-` без единицы.
LBM_API std::string_view unitText(Unit unit) noexcept;
LBM_API Unit parseUnit(std::string_view text) noexcept;
/// Значение выше — лучше (пропускная способность).
inline bool higherIsBetter(const ResultRow& row) noexcept { return row.unit == "B/s"; }

/// Ключ строки без значения: по нему сливаются и сравниваются таблицы.
LBM_API std::string rowKey(const ResultRow& row);
/// Ключ — без `variant` и `unit`: они выводятся из остальных.
/// Стабильный порядок: group, bench, implementation, params, n (числом), stage, metric.
LBM_API void sortRows(std::vector<ResultRow>& rows);

/// TSV со стабильным порядком строк и 7 значащими цифрами.
LBM_API std::string writeTsv(const ResultTable& table);
/// Разбор TSV; ошибка — Exception с номером строки.
LBM_API ResultTable parseTsv(std::string_view text);

}
