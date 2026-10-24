#pragma once

#include <format>
#include <string>

#include <BmRunner/Benchmarks.hpp>

namespace Lattice::Benchmarks {

/// Сборка набора: компилятор и режим берутся из единицы трансляции набора, а не из
/// библиотеки BmRunner, — поэтому функция встраиваемая.
inline std::string buildDescription() {
#if defined(__clang__)
    std::string compiler = std::format("clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    std::string compiler = std::format("gcc {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    std::string compiler = std::format("msvc {}", _MSC_VER);
#else
    std::string compiler = "unknown compiler";
#endif
#if defined(NDEBUG)
    constexpr const char* mode = "release";
#else
    constexpr const char* mode = "debug";
#endif
#if defined(__x86_64__) || defined(_M_X64)
    constexpr const char* arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    constexpr const char* arch = "arm64";
#else
    constexpr const char* arch = "unknown";
#endif
    return std::format("{}, {}, {}", mode, compiler, arch);
}

/// Точка входа набора бенчмарков:
///     int main(int argc, char** argv) { return Lattice::Benchmarks::main(argc, argv); }
/// Аргументы: `[filter] [--filter <группа|бенч|маска>] [--tsv <файл>] [--list]`.
inline int main(int argc, char** argv) {
    return runMain(argc, argv, buildDescription());
}

}
