#pragma once

// Windows: GetLogicalProcessorInformationEx, GlobalMemoryStatusEx, DXGI. Included only from the
// library's .cpp files: <windows.h> must not reach its users. A logical CPU is group * 64 + bit,
// as thread affinity takes it. 0 means unknown: the minimum frequency, the size of a NUMA node
// when there are several.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#include <powrprof.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__)
#include <intrin.h>
#endif

#include "../SystemInfo.hpp"

namespace Lattice::SystemInfo::Platform {

namespace Windows {

inline std::string narrow(std::wstring_view text) {
    if (text.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size_t(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

inline std::vector<std::byte> logicalProcessors(LOGICAL_PROCESSOR_RELATIONSHIP relation) {
    DWORD length = 0;
    GetLogicalProcessorInformationEx(relation, nullptr, &length);
    std::vector<std::byte> buffer(length);
    if (!length || !GetLogicalProcessorInformationEx(
                       relation, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &length))
        return {};
    buffer.resize(length);
    return buffer;
}

template<class F>
void forEach(const std::vector<std::byte>& buffer, F&& visit) {
    for (size_t offset = 0; offset < buffer.size();) {
        const auto& info = *reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        visit(info);
        offset += info.Size;
    }
}

inline void appendCpus(const GROUP_AFFINITY& mask, std::vector<uint32_t>& cpus) {
    for (uint32_t bit = 0; bit < sizeof(KAFFINITY) * 8; ++bit)
        if (mask.Mask & (KAFFINITY(1) << bit))
            cpus.push_back(uint32_t(mask.Group) * 64 + bit);
}

inline std::string processorName() {
    wchar_t name[256]{};
    DWORD size = sizeof(name);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size) != ERROR_SUCCESS)
        return {};
    std::string text = narrow(name);
    const size_t first = text.find_first_not_of(' ');
    const size_t last = text.find_last_not_of(' ');
    return first == std::string::npos ? std::string{} : text.substr(first, last - first + 1);
}

inline CpuFeature features() {
    // PF_* values from winnt.h; older SDKs lack the newer names.
    struct Flag { DWORD feature; CpuFeature value; };
    constexpr Flag flags[] = {
        {6, CpuFeature::SSE},     {10, CpuFeature::SSE2},  {13, CpuFeature::SSE3},  {36, CpuFeature::SSSE3},
        {37, CpuFeature::SSE41},  {38, CpuFeature::SSE42}, {39, CpuFeature::AVX},   {40, CpuFeature::AVX2},
        {41, CpuFeature::AVX512F}, {19, CpuFeature::NEON}, {46, CpuFeature::SVE}};
    CpuFeature result = CpuFeature::None;
    for (const auto& flag : flags)
        if (IsProcessorFeaturePresent(flag.feature))
            result |= flag.value;
#if defined(_M_X64) || defined(__x86_64__)
    int registers[4]{};
    __cpuid(registers, 1);
    if (registers[2] & (1 << 12))
        result |= CpuFeature::FMA;
#endif
    return result;
}

inline uint64_t maxFrequencyHz(size_t logicalCount) {
    // Not declared by the SDK: the layout CallNtPowerInformation documents.
    struct ProcessorPower { ULONG number, maxMhz, currentMhz, mhzLimit, maxIdleState, currentIdleState; };
    std::vector<ProcessorPower> power(logicalCount);
    if (power.empty() || CallNtPowerInformation(ProcessorInformation, nullptr, 0, power.data(),
                                                ULONG(power.size() * sizeof(ProcessorPower))) != 0)
        return 0;
    ULONG mhz = 0;
    for (const auto& processor : power)
        mhz = std::max(mhz, processor.maxMhz);
    return uint64_t(mhz) * 1'000'000ull;
}

inline CacheType cacheType(PROCESSOR_CACHE_TYPE type) {
    if (type == CacheData) return CacheType::Data;
    if (type == CacheInstruction) return CacheType::Instruction;
    return CacheType::Unified;
}

}

inline std::vector<ProcessorInfo> processors() {
    using namespace Windows;
    const auto all = logicalProcessors(RelationAll);
    std::vector<ProcessorInfo> result;
    std::vector<std::vector<uint32_t>> packageCpus;

    forEach(all, [&](const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX& info) {
        if (info.Relationship != RelationProcessorPackage)
            return;
        auto& cpus = packageCpus.emplace_back();
        for (WORD group = 0; group < info.Processor.GroupCount; ++group)
            appendCpus(info.Processor.GroupMask[group], cpus);
        result.push_back({.id = uint32_t(result.size()), .arch = architecture()});
    });
    const auto packageOf = [&](uint32_t cpu) -> ProcessorInfo* {
        for (size_t i = 0; i < packageCpus.size(); ++i)
            if (std::ranges::contains(packageCpus[i], cpu))
                return &result[i];
        return nullptr;
    };

    size_t logicalCount = 0;
    forEach(all, [&](const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX& info) {
        std::vector<uint32_t> cpus;
        if (info.Relationship == RelationProcessorCore) {
            appendCpus(info.Processor.GroupMask[0], cpus);
            logicalCount += cpus.size();
            if (auto* package = cpus.empty() ? nullptr : packageOf(cpus.front()))
                package->cores.push_back({.id = uint32_t(package->cores.size()), .logicalCpus = std::move(cpus)});
        } else if (info.Relationship == RelationCache) {
            const CACHE_RELATIONSHIP& cache = info.Cache;
            if (cache.Type == CacheTrace)
                return;
            appendCpus(cache.GroupMask, cpus);
            std::ranges::sort(cpus);
            auto* package = cpus.empty() ? nullptr : packageOf(cpus.front());
            if (!package)
                return;
            const bool fully = cache.Associativity == CACHE_FULLY_ASSOCIATIVE;
            package->caches.push_back({
                .level = cache.Level,
                .type = cacheType(cache.Type),
                .sizeBytes = cache.CacheSize,
                .lineSizeBytes = cache.LineSize,
                .ways = fully ? 0u : cache.Associativity,
                .sets = fully || !cache.Associativity || !cache.LineSize
                            ? 0u
                            : uint32_t(cache.CacheSize / (uint32_t(cache.Associativity) * cache.LineSize)),
                .sharedLogicalCpus = std::move(cpus)});
        }
    });

    const std::string name = processorName();
    const CpuFeature flags = features();
    const uint64_t maxHz = maxFrequencyHz(logicalCount);
    for (auto& processor : result) {
        processor.name = name;
        processor.features = flags;
        processor.maxFrequencyHz = maxHz;
    }
    return result;
}

inline MemoryInfo memory() {
    using namespace Windows;
    MemoryInfo info;
    MEMORYSTATUSEX status{.dwLength = sizeof(MEMORYSTATUSEX)};
    if (GlobalMemoryStatusEx(&status)) {
        info.totalBytes = status.ullTotalPhys;
        info.availableBytes = status.ullAvailPhys;
    }
    forEach(logicalProcessors(RelationNumaNode), [&](const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX& entry) {
        MemoryNodeInfo node{.id = entry.NumaNode.NodeNumber};
        appendCpus(entry.NumaNode.GroupMask, node.logicalCpus);
        ULONGLONG available = 0;
        if (GetNumaAvailableMemoryNodeEx(USHORT(node.id), &available))
            node.freeBytes = available;
        info.nodes.push_back(std::move(node));
    });
    if (info.nodes.size() == 1)
        info.nodes.front().totalBytes = info.totalBytes;
    return info;
}

inline std::vector<GpuInfo> gpus() {
    using namespace Windows;
    std::vector<GpuInfo> result;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
        return result;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            result.push_back({.name = narrow(desc.Description), .vramBytes = desc.DedicatedVideoMemory});
        adapter->Release();
    }
    factory->Release();
    return result;
}

inline OsInfo os() {
    OsInfo info;
    // GetVersionEx reports the version the manifest asks for; RtlGetVersion — the real one.
    using RtlGetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW version{.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW)};
    if (const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (const auto get = reinterpret_cast<RtlGetVersion>(GetProcAddress(ntdll, "RtlGetVersion")); get && get(&version) == 0) {
            info.kernel = std::format("{}.{}.{}", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
            info.name = version.dwMajorVersion == 10 ? (version.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10")
                                                     : std::format("Windows {}.{}", version.dwMajorVersion,
                                                                   version.dwMinorVersion);
        }
    if (info.name.empty())
        info.name = "Windows";
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    switch (system.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: info.architecture = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: info.architecture = "aarch64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: info.architecture = "x86"; break;
        default: break;
    }
    return info;
}

inline std::filesystem::path executable() {
    std::vector<wchar_t> path(32768);
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (size == 0 || size >= path.size())
        return {};
    return std::filesystem::path(path.data(), path.data() + size);
}

}
