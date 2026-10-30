#pragma once

#include <string_view>
#include <vector>

#include <lbm-runner/Bench.hpp>
#include <lbm-runner/BenchTypes.hpp>
#include <lbm-runner/Results.hpp>
#include <lbm-runner/Work.hpp>
#include <lattice-tools/Fixture.hpp>
#include <lbm-runner/Visibility.hpp>


/**
 @file Benchmarks.hpp
 @brief Регистрация и запуск бенчмарков.

 Предоставляет макросы автоматической регистрации бенчмарков, групповых
 конфигураций, функции запуска зарегистрированных тестов, получения их списка
 и настройки callback-ов для обработки результатов выполнения.
*/


#define BENCH2(name, FixtureType) \
    BENCH3(name, FixtureType, "")

#define BENCH3(name, FixtureType, description) \
    static void name(::Lattice::Benchmarks::Bench& bench); \
    static std::unique_ptr<::Lattice::Fixture> _fixture_##name(size_t n) { \
        return std::make_unique<FixtureType>(n); \
    } \
    static ::Lattice::Benchmarks::Registrar _bench_##name( \
        "", \
        #name, \
        description, \
        name, \
        _fixture_##name \
    ); \
    static void name(::Lattice::Benchmarks::Bench& bench)

#define BENCH_SELECT(_1, _2, _3, NAME, ...) NAME
#define BENCH(...) BENCH_SELECT(__VA_ARGS__, BENCH3, BENCH2)(__VA_ARGS__)
    

#define BENCH_GROUPED3(group, name, FixtureType) \
    BENCH_GROUPED4(group, name, FixtureType, "")

#define BENCH_GROUPED4(group, name, FixtureType, description) \
    static void name(::Lattice::Benchmarks::Bench& bench); \
    static std::unique_ptr<::Lattice::Fixture> _fixture_##name(size_t n) { \
        return std::make_unique<FixtureType>(n); \
    } \
    static ::Lattice::Benchmarks::Registrar _bench_##name( \
        #group, \
        #name, \
        description, \
        name, \
        _fixture_##name \
    ); \
    static void name(::Lattice::Benchmarks::Bench& bench)

#define BENCH_GROUPED_SELECT(_1, _2, _3, _4, NAME, ...) NAME
#define BENCH_GROUPED(...) \
    BENCH_GROUPED_SELECT(__VA_ARGS__, BENCH_GROUPED4, BENCH_GROUPED3)(__VA_ARGS__)


#define BENCH_GROUP(group) \
    static void _bench_group_config_##group(::Lattice::Benchmarks::Bench& bench); \
    static ::Lattice::Benchmarks::GroupRegistrar _bench_group_registrar_##group( \
        #group, \
        _bench_group_config_##group \
    ); \
    static void _bench_group_config_##group(::Lattice::Benchmarks::Bench& bench)


namespace Lattice::Benchmarks {

struct Info {
    std::string_view group;
    std::string_view name;
    std::string_view description;
};

class LBM_API Registrar {
public:
    Registrar(
        std::string_view group,
        std::string_view name,
        std::string_view description,
        void (*function)(Bench&),
        Fixture::Factory createFixture
    );
};

class LBM_API GroupRegistrar {
public:
    GroupRegistrar(
        std::string_view group,
        void (*function)(Bench&)
    );
};

LBM_API void run(std::string_view name);
/// Бенчи по фильтру: пусто — все; имя группы или бенча; маска `*`/`?` по `group/name`.
/// Печатает как обычно и возвращает таблицу результатов со сводкой исполнителей.
LBM_API ResultTable runFiltered(std::string_view filter);
/// Точка входа набора (см. BmRunner/Main.hpp):
/// `[filter] [--filter f] [--tsv file] [--sweep axis] [--axis axis=v1,v2] [--list]`.
/// CLI output: default progress only, -v text + progress, -vv legacy text.
/// Progress is terminal-only; runFiltered/run/runAll keep their existing callbacks/output.
LBM_API int runMain(int argc, char** argv, std::string_view build);
LBM_API void runGroup(std::string_view group);
LBM_API void runAll();

LBM_API std::vector<Info> list();
LBM_API std::vector<std::string_view> groups();

LBM_API void setSampleCallback(SampleCallback callback);
LBM_API void setResultCallback(ResultCallback callback);
LBM_API void setCompleteCallback(CompleteCallback callback);
/// Сводка работ, измеренных на нескольких исполнителях (runGroup / runAll).
LBM_API void setComparisonCallback(ComparisonCallback callback);
LBM_API void disableComparisonCallback();

LBM_API void disableSampleCallback();
LBM_API void disableResultCallback();
LBM_API void disableCompleteCallback();
LBM_API void disableCallbacks();
LBM_API void resetCallbacks();

}
