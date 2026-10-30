#pragma once

// macOS: sysctl, Mach and IOKit. Included only from the library's .cpp files. One memory node
// (no NUMA); logical CPU numbers are labels — macOS gives no thread affinity, so they map to
// no OS core. Apple Silicon: cores by performance level (perflevel0 — the fastest), its GPU
// shares system memory (vramBytes 0). 0 means unknown.

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include "../SystemInfo.hpp"

namespace Lattice::SystemInfo::Platform {

namespace Apple {

/// An integer sysctl of any width (4 or 8 bytes, little-endian); 0 when absent.
inline uint64_t number(const std::string& name) {
    uint64_t value = 0;
    size_t size = sizeof(value);
    return sysctlbyname(name.c_str(), &value, &size, nullptr, 0) == 0 ? value : 0;
}

inline std::string text(const char* name) {
    size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0)
        return {};
    std::string value(size, '\0');
    if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0)
        return {};
    value.resize(std::strlen(value.c_str()));
    return value;
}

inline CpuFeature features() {
    struct Flag { const char* name; CpuFeature value; };
    constexpr Flag flags[] = {
        {"hw.optional.sse", CpuFeature::SSE},      {"hw.optional.sse2", CpuFeature::SSE2},
        {"hw.optional.sse3", CpuFeature::SSE3},    {"hw.optional.supplementalsse3", CpuFeature::SSSE3},
        {"hw.optional.sse4_1", CpuFeature::SSE41}, {"hw.optional.sse4_2", CpuFeature::SSE42},
        {"hw.optional.avx1_0", CpuFeature::AVX},   {"hw.optional.avx2_0", CpuFeature::AVX2},
        {"hw.optional.fma", CpuFeature::FMA},      {"hw.optional.avx512f", CpuFeature::AVX512F},
        {"hw.optional.neon", CpuFeature::NEON},    {"hw.optional.AdvSIMD", CpuFeature::NEON}};
    CpuFeature result = CpuFeature::None;
    for (const auto& flag : flags)
        if (number(flag.name))
            result |= flag.value;
    return result;
}

/// Caches of `count` logical CPUs from `first`, shared by `perCache` of them (0 — by all).
inline void addCaches(ProcessorInfo& processor, uint8_t level, CacheType type, uint64_t size, uint32_t line,
                      uint32_t first, uint32_t count, uint32_t perCache) {
    if (!size || !count)
        return;
    const uint32_t step = perCache ? perCache : count;
    for (uint32_t begin = first; begin < first + count; begin += step) {
        CacheInfo cache{.level = level, .type = type, .sizeBytes = size, .lineSizeBytes = line};
        for (uint32_t cpu = begin; cpu < std::min(begin + step, first + count); ++cpu)
            cache.sharedLogicalCpus.push_back(cpu);
        processor.caches.push_back(std::move(cache));
    }
}

inline std::string cfText(CFTypeRef value) {
    if (!value)
        return {};
    std::string result;
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        char buffer[256]{};
        if (CFStringGetCString(static_cast<CFStringRef>(value), buffer, sizeof(buffer), kCFStringEncodingUTF8))
            result = buffer;
    } else if (CFGetTypeID(value) == CFDataGetTypeID()) {
        const auto data = static_cast<CFDataRef>(value);
        result.assign(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), size_t(CFDataGetLength(data)));
        result.resize(std::strlen(result.c_str()));
    }
    return result;
}

/// A property of the entry or, failing that, of one of its parents.
inline CFTypeRef inherited(io_registry_entry_t entry, CFStringRef key) {
    return IORegistryEntrySearchCFProperty(entry, kIOServicePlane, key, kCFAllocatorDefault,
                                           kIORegistryIterateRecursively | kIORegistryIterateParents);
}

}

inline std::vector<ProcessorInfo> processors() {
    using namespace Apple;
    ProcessorInfo processor{.id = 0, .name = text("machdep.cpu.brand_string"), .arch = architecture(),
                            .features = features(), .minFrequencyHz = number("hw.cpufrequency_min"),
                            .maxFrequencyHz = number("hw.cpufrequency_max")};
    const auto line = uint32_t(number("hw.cachelinesize"));
    uint32_t next = 0;
    const auto addLevel = [&](uint32_t physical, uint32_t logical, uint64_t l1d, uint64_t l1i, uint64_t l2,
                              uint32_t perL2) {
        if (!physical)
            return;
        const uint32_t threads = std::max(1u, logical / physical);
        for (uint32_t core = 0; core < physical; ++core) {
            CpuCoreInfo info{.id = uint32_t(processor.cores.size())};
            for (uint32_t thread = 0; thread < threads; ++thread)
                info.logicalCpus.push_back(next + core * threads + thread);
            processor.cores.push_back(std::move(info));
        }
        addCaches(processor, 1, CacheType::Data, l1d, line, next, physical * threads, threads);
        addCaches(processor, 1, CacheType::Instruction, l1i, line, next, physical * threads, threads);
        addCaches(processor, 2, CacheType::Unified, l2, line, next, physical * threads, perL2);
        next += physical * threads;
    };

    if (const auto levels = uint32_t(number("hw.nperflevels")); levels > 0) {
        for (uint32_t level = 0; level < levels; ++level) {
            const std::string prefix = "hw.perflevel" + std::to_string(level) + ".";
            addLevel(uint32_t(number(prefix + "physicalcpu")), uint32_t(number(prefix + "logicalcpu")),
                     number(prefix + "l1dcachesize"), number(prefix + "l1icachesize"), number(prefix + "l2cachesize"),
                     uint32_t(number(prefix + "cpusperl2")));
        }
    } else {
        // Intel Macs: per-core L1 and L2.
        const auto physical = uint32_t(number("hw.physicalcpu"));
        const auto logical = uint32_t(number("hw.logicalcpu"));
        addLevel(physical, logical, number("hw.l1dcachesize"), number("hw.l1icachesize"), number("hw.l2cachesize"),
                 std::max(1u, logical / std::max(1u, physical)));
    }
    addCaches(processor, 3, CacheType::Unified, number("hw.l3cachesize"), line, 0, next, 0);
    return {std::move(processor)};
}

inline MemoryInfo memory() {
    using namespace Apple;
    MemoryInfo info{.totalBytes = number("hw.memsize")};
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) ==
        KERN_SUCCESS)
        info.availableBytes = (uint64_t(vm.free_count) + vm.inactive_count) * vm_page_size;
    MemoryNodeInfo node{.id = 0, .totalBytes = info.totalBytes, .freeBytes = info.availableBytes};
    for (uint32_t cpu = 0; cpu < uint32_t(number("hw.logicalcpu")); ++cpu)
        node.logicalCpus.push_back(cpu);
    info.nodes.push_back(std::move(node));
    return info;
}

inline std::vector<GpuInfo> gpus() {
    using namespace Apple;
    std::vector<GpuInfo> result;
    io_iterator_t iterator = 0;
    if (IOServiceGetMatchingServices(MACH_PORT_NULL, IOServiceMatching("IOAccelerator"), &iterator) != KERN_SUCCESS)
        return result;
    while (const io_object_t service = IOIteratorNext(iterator)) {
        GpuInfo gpu;
        if (const CFTypeRef model = inherited(service, CFSTR("model"))) {
            gpu.name = cfText(model);
            CFRelease(model);
        }
        if (const CFTypeRef driver = IORegistryEntryCreateCFProperty(service, CFSTR("IOClass"), kCFAllocatorDefault, 0)) {
            gpu.driver = cfText(driver);
            CFRelease(driver);
        }
        if (const CFTypeRef vram = inherited(service, CFSTR("VRAM,totalMB"))) {
            int64_t megabytes = 0;
            if (CFGetTypeID(vram) == CFNumberGetTypeID() &&
                CFNumberGetValue(static_cast<CFNumberRef>(vram), kCFNumberSInt64Type, &megabytes))
                gpu.vramBytes = uint64_t(megabytes) << 20;
            CFRelease(vram);
        }
        result.push_back(std::move(gpu));
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    return result;
}

inline OsInfo os() {
    OsInfo info{.name = "macOS " + Apple::text("kern.osproductversion")};
    utsname system{};
    if (uname(&system) == 0) {
        info.kernel = system.release;
        // The same name as Linux and Windows report.
        info.architecture = std::string_view(system.machine) == "arm64" ? "aarch64" : system.machine;
    }
    return info;
}

inline std::filesystem::path executable() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    return _NSGetExecutablePath(path.data(), &size) == 0 ? std::filesystem::path(path.data())
                                                         : std::filesystem::path{};
}

}
