#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <BmRunner/Analysis.hpp>
#include <BmRunner/BenchTypes.hpp>
#include <BmRunner/Visibility.hpp>

namespace Lattice::Benchmarks {

/**
 @file Results.hpp
 @brief Результаты запуска как таблица: строка на значение метрики, формат TSV.

 Длинный вид: `group bench work executor mode n stage metric value samples measured`.
 Новая метрика, фаза или стадия — новые строки, а не новая схема; построчный diff
 показывает, какое значение какого бенча изменилось. Анализ — строки со стадией
 `analysis`, сводка исполнителей — `compare`; у них `n` = `-`.
*/

struct ResultRow {
    std::string group;
    std::string bench;      // `-` у строк сравнения исполнителей
    std::string work;
    std::string executor;   // `-`, если бенч не участвует в сравнении
    std::string mode;       // `-` для Mode::Default
    std::string n;          // число или `-`
    std::string stage;      // имя стадии, `analysis`, `compare`
    std::string metric;     // `Capability.name`, у анализа `y(x).value`
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
BMRUNNER_API std::vector<ResultRow> resultRows(const BenchResult& result);
/// Строки сводки исполнителей: подгонка каждого и пороги `crossover:<slower>`.
BMRUNNER_API std::vector<ResultRow> resultRows(const Comparison& comparison);

/// Ключ строки без значения: по нему сливаются и сравниваются таблицы.
BMRUNNER_API std::string rowKey(const ResultRow& row);
/// Стабильный порядок: group, bench, n (числом), stage, metric, executor.
BMRUNNER_API void sortRows(std::vector<ResultRow>& rows);

/// TSV со стабильным порядком строк и 7 значащими цифрами.
BMRUNNER_API std::string writeTsv(const ResultTable& table);
/// Разбор TSV; ошибка — Exception с номером строки.
BMRUNNER_API ResultTable parseTsv(std::string_view text);

}
