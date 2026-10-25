#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <lattice-sysinfo/SystemInfo.hpp>

namespace Lattice::Benchmarks {

/// Самый большой кеш процессора (обычно LLC) по SysInfo; 64 МиБ, если неизвестен.
inline uint64_t largestCache() {
    static const uint64_t size = [] {
        uint64_t largest = 0;
        for (const auto& processor : SystemInfo::collectCPU())
            for (const auto& cache : processor.caches)
                largest = std::max(largest, cache.sizeBytes);
        return largest ? largest : uint64_t(64) << 20;
    }();
    return size;
}

/// Холодный кеш (режим Offload на CPU): запись и чтение буфера вдвое больше самого
/// большого кеша вытесняют рабочий набор, и итерация платит за перенос из памяти.
/// Вызывается из Fixture::prepare — вне замера.
inline void evictCaches() {
    static std::vector<uint64_t> buffer(2 * largestCache() / sizeof(uint64_t));
    uint64_t sum = 0;
    for (uint64_t& value : buffer) {
        value += 1;
        sum += value;
    }
    asm volatile("" : : "r"(sum) : "memory");
}

}
