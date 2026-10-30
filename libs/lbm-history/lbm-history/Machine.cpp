#include "Machine.hpp"

#include <cctype>
#include <cstdint>
#include <format>

#include <lattice-sysinfo/SystemInfo.hpp>

#include "platform/Platform.hpp"

namespace Lattice::BmHistory {

namespace {

uint32_t fnv1a(std::string_view text) {
    uint32_t hash = 2166136261u;
    for (const unsigned char c : text) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

std::string tomlString(std::string_view value) {
    std::string result = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\')
            result += '\\';
        result += c;
    }
    return result + "\"";
}

std::string hostName() {
    const std::string name = Platform::systemHostName();
    if (name.empty())
        return "host";
    std::string host;
    for (const char* c = name.c_str(); *c && *c != '.'; ++c)
        host += std::isalnum(static_cast<unsigned char>(*c)) || *c == '-' ? char(std::tolower(*c)) : '-';
    return host;
}

}

Machine currentMachine() {
    const auto processors = SystemInfo::collectCPU();
    const auto memory = SystemInfo::collectMemory();
    const auto gpus = SystemInfo::collectGpus();

    // Only what changes the numbers: no kernel version, no free memory.
    std::string fingerprint;
    std::string description = "# Machine of the benchmark results in this folder.\n";
    for (const auto& cpu : processors) {
        fingerprint += std::format("cpu {} {} {}\n", cpu.name, cpu.cores.size(), cpu.maxFrequencyHz);
        description += std::format("\n[[cpu]]\nname = {}\ncores = {}\nmax_frequency_hz = {}\n",
            tomlString(cpu.name), cpu.cores.size(), cpu.maxFrequencyHz);
        for (const auto& cache : cpu.caches) {
            fingerprint += std::format("cache L{} {} {}\n", cache.level, int(cache.type), cache.sizeBytes);
            description += std::format("cache_l{}_{} = {}\n", cache.level, int(cache.type), cache.sizeBytes);
        }
    }
    fingerprint += std::format("memory {}\n", memory.totalBytes >> 30);
    description += std::format("\n[memory]\ntotal_gib = {}\n", memory.totalBytes >> 30);
    for (const auto& gpu : gpus) {
        fingerprint += std::format("gpu {} {} {} {}\n", gpu.name, gpu.driver, gpu.computeInfo, gpu.vramBytes);
        description += std::format("\n[[gpu]]\nname = {}\ndriver = {}\ncompute = {}\nvram_bytes = {}\n",
            tomlString(gpu.name), tomlString(gpu.driver), tomlString(gpu.computeInfo), gpu.vramBytes);
    }

    const std::string host = hostName();
    const std::string id = std::format("{}-{:08x}", host, fnv1a(fingerprint));
    return {.id = id, .description = std::format("id = {}\nhost = {}\n", tomlString(id), tomlString(host)) + description};
}

}
