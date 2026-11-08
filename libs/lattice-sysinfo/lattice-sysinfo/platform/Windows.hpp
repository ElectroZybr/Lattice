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
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cfgmgr32.h>
#include <devpropdef.h>
#include <dxgi.h>
#include <iphlpapi.h>
#include <powrprof.h>
#include <setupapi.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
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

namespace Windows {

// {3AB22E31-...}: DEVPKEY_PciDevice_*; pciprop.h is not in the plain SDK.
inline std::optional<uint32_t> pciProperty(DEVINST node, ULONG id) {
    const DEVPROPKEY key{{0x3AB22E31, 0x8264, 0x4B4E, {0x9A, 0xF5, 0xA8, 0xD2, 0xD8, 0xE3, 0x3E, 0x62}}, id};
    DEVPROPTYPE type = 0;
    uint32_t value = 0;
    ULONG size = sizeof value;
    if (CM_Get_DevNode_PropertyW(node, &key, &type, reinterpret_cast<PBYTE>(&value), &size, 0) != CR_SUCCESS ||
        type != DEVPROP_TYPE_UINT32)
        return std::nullopt;
    return value;
}

inline uint32_t hexAfter(const std::string& text, std::string_view label) {
    const size_t at = text.find(label);
    if (at == std::string::npos)
        return 0;
    uint32_t value = 0;
    const char* begin = text.data() + at + label.size();
    std::from_chars(begin, text.data() + text.size(), value, 16);
    return value;
}

// The display devices on PCI, by their ids. The speed is the most the device can; the width in
// use, since a laptop wires an x16 chip with fewer lanes and the port above tells nothing.
inline void readPcie(GpuInfo& gpu) {
    // GUID_DEVCLASS_DISPLAY
    static const GUID display{0x4D36E968, 0xE325, 0x11CE, {0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18}};
    const HDEVINFO devices = SetupDiGetClassDevsW(&display, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE)
        return;
    SP_DEVINFO_DATA device{.cbSize = sizeof(SP_DEVINFO_DATA)};
    for (DWORD index = 0; SetupDiEnumDeviceInfo(devices, index, &device); ++index) {
        wchar_t id[512]{};
        if (!SetupDiGetDeviceInstanceIdW(devices, &device, id, DWORD(std::size(id)), nullptr))
            continue;
        const std::string text = narrow(id);
        if (!text.starts_with("PCI\\") || hexAfter(text, "VEN_") != gpu.vendorId || hexAfter(text, "DEV_") != gpu.deviceId)
            continue;
        const auto generation = pciProperty(device.DevInst, 11);
        const auto lanes = pciProperty(device.DevInst, 10);
        if (generation && lanes && *generation && *lanes)
            gpu.pcie = PcieLink{*generation, *lanes};
        break;
    }
    SetupDiDestroyDeviceInfoList(devices);
}

inline void useWinsock() {
    static const bool started = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)started;
}

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
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            GpuInfo gpu{.name = narrow(desc.Description),
                        .vramBytes = desc.DedicatedVideoMemory,
                        .vendorId = desc.VendorId,
                        .deviceId = desc.DeviceId};
            readPcie(gpu);
            result.push_back(std::move(gpu));
        }
        adapter->Release();
    }
    factory->Release();
    return result;
}

inline std::vector<NetworkInfo> networks() {
    using namespace Windows;
    std::vector<NetworkInfo> result;
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 32 * 1024;
    std::vector<std::byte> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG status = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size);
    if (status == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        status = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size);
    }
    if (status != NO_ERROR)
        return result;
    for (const auto* adapter = adapters; adapter; adapter = adapter->Next) {
        NetworkInfo network{.name = narrow(adapter->FriendlyName), .up = adapter->OperStatus == IfOperStatusUp};
        MIB_IF_ROW2 row{};
        row.InterfaceLuid = adapter->Luid;
        const bool hardware = GetIfEntry2(&row) == NO_ERROR && row.InterfaceAndOperStatusFlags.HardwareInterface;
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            network.kind = NetworkKind::Loopback;
        else if (!hardware)
            network.kind = NetworkKind::Virtual;
        else if (adapter->IfType == IF_TYPE_IEEE80211)
            network.kind = NetworkKind::Wireless;
        else if (adapter->IfType == IF_TYPE_ETHERNET_CSMACD)
            network.kind = NetworkKind::Ethernet;
        const ULONG64 speed = adapter->TransmitLinkSpeed;
        if (speed > 0 && speed != std::numeric_limits<ULONG64>::max() && hardware &&
            adapter->IfType != IF_TYPE_SOFTWARE_LOOPBACK)
            network.megabits = speed / 1'000'000;
        for (const auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            const sockaddr* address = unicast->Address.lpSockaddr;
            char text[INET6_ADDRSTRLEN]{};
            const void* source = address->sa_family == AF_INET
                ? static_cast<const void*>(&reinterpret_cast<const sockaddr_in*>(address)->sin_addr)
                : static_cast<const void*>(&reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr);
            if (inet_ntop(address->sa_family, source, text, sizeof text))
                network.addresses.emplace_back(text);
        }
        result.push_back(std::move(network));
    }
    return result;
}

inline std::optional<std::string> localAddressTo(const std::string& address) {
    using namespace Windows;
    useWinsock();
    sockaddr_storage target{};
    int size = sizeof(sockaddr_in);
    auto* v4 = reinterpret_cast<sockaddr_in*>(&target);
    auto* v6 = reinterpret_cast<sockaddr_in6*>(&target);
    if (inet_pton(AF_INET, address.c_str(), &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET;
        v4->sin_port = htons(9);
    } else if (inet_pton(AF_INET6, address.c_str(), &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(9);
        size = sizeof(sockaddr_in6);
    } else {
        return std::nullopt;
    }
    const SOCKET handle = socket(target.ss_family, SOCK_DGRAM, 0);
    if (handle == INVALID_SOCKET)
        return std::nullopt;
    sockaddr_storage local{};
    int localSize = sizeof local;
    const bool routed = connect(handle, reinterpret_cast<sockaddr*>(&target), size) == 0 &&
        getsockname(handle, reinterpret_cast<sockaddr*>(&local), &localSize) == 0;
    closesocket(handle);
    if (!routed)
        return std::nullopt;
    char text[INET6_ADDRSTRLEN]{};
    const void* source = local.ss_family == AF_INET
        ? static_cast<const void*>(&reinterpret_cast<sockaddr_in*>(&local)->sin_addr)
        : static_cast<const void*>(&reinterpret_cast<sockaddr_in6*>(&local)->sin6_addr);
    if (!inet_ntop(local.ss_family, source, text, sizeof text))
        return std::nullopt;
    return std::string{text};
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
