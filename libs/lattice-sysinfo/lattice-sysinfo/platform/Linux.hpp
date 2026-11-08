#pragma once

// Linux: /proc and /sys. Included only from Collect.cpp.

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "../SystemInfo.hpp"

namespace Lattice::SystemInfo::Platform {

namespace Linux {

namespace fs = std::filesystem;

inline std::string trim(std::string_view value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const size_t last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

inline std::string readText(const fs::path& path) {
    std::ifstream file(path);
    std::string value;
    if (!file || !std::getline(file, value))
        return {};
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' '))
        value.pop_back();
    return value;
}

inline uint64_t readUint(const fs::path& path) {
    const std::string value = readText(path);
    uint64_t result = 0;
    if (value.empty())
        return 0;
    const auto [_, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    return ec == std::errc{} ? result : 0;
}

inline uint64_t parseSize(std::string_view value) {
    if (value.empty())
        return 0;
    uint64_t multiplier = 1;
    switch (value.back()) {
        case 'K': multiplier = 1024ull; value.remove_suffix(1); break;
        case 'M': multiplier = 1024ull * 1024ull; value.remove_suffix(1); break;
        case 'G': multiplier = 1024ull * 1024ull * 1024ull; value.remove_suffix(1); break;
    }
    uint64_t result = 0;
    const auto [_, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    return ec == std::errc{} ? result * multiplier : 0;
}

inline std::vector<uint32_t> parseCpuList(std::string_view value) {
    std::vector<uint32_t> result;
    while (!value.empty()) {
        const size_t comma = value.find(',');
        const std::string_view part = value.substr(0, comma);
        const size_t dash = part.find('-');
        if (dash == std::string_view::npos) {
            uint32_t cpu = 0;
            if (std::from_chars(part.data(), part.data() + part.size(), cpu).ec == std::errc{})
                result.push_back(cpu);
        } else {
            uint32_t first = 0, last = 0;
            const auto a = part.substr(0, dash);
            const auto b = part.substr(dash + 1);
            if (std::from_chars(a.data(), a.data() + a.size(), first).ec == std::errc{} &&
                std::from_chars(b.data(), b.data() + b.size(), last).ec == std::errc{}) {
                for (uint32_t cpu = first; cpu <= last; ++cpu)
                    result.push_back(cpu);
            }
        }
        if (comma == std::string_view::npos)
            break;
        value.remove_prefix(comma + 1);
    }
    return result;
}

inline uint64_t parseKbLine(std::string_view line) {
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos)
        return 0;
    line.remove_prefix(colon + 1);
    while (!line.empty() && line.front() == ' ')
        line.remove_prefix(1);
    uint64_t kb = 0;
    const auto [_, ec] = std::from_chars(line.data(), line.data() + line.size(), kb);
    return ec == std::errc{} ? kb * 1024ull : 0;
}

inline CpuFeature parseFeatures(std::string_view text) {
    CpuFeature features = CpuFeature::None;
    std::istringstream stream{std::string(text)};
    std::string flag;
    while (stream >> flag) {
        if (flag == "sse") features |= CpuFeature::SSE;
        else if (flag == "sse2") features |= CpuFeature::SSE2;
        else if (flag == "pni") features |= CpuFeature::SSE3;
        else if (flag == "ssse3") features |= CpuFeature::SSSE3;
        else if (flag == "sse4_1") features |= CpuFeature::SSE41;
        else if (flag == "sse4_2") features |= CpuFeature::SSE42;
        else if (flag == "avx") features |= CpuFeature::AVX;
        else if (flag == "avx2") features |= CpuFeature::AVX2;
        else if (flag == "fma") features |= CpuFeature::FMA;
        else if (flag == "avx512f") features |= CpuFeature::AVX512F;
        else if (flag == "asimd") features |= CpuFeature::NEON;
        else if (flag == "sve") features |= CpuFeature::SVE;
    }
    return features;
}

inline CacheType parseCacheType(std::string_view value) {
    if (value == "Data") return CacheType::Data;
    if (value == "Instruction") return CacheType::Instruction;
    return CacheType::Unified;
}

struct ProcCpuInfo {
    uint32_t id = 0;
    std::string name;
    CpuFeature features = CpuFeature::None;
};

inline std::vector<ProcCpuInfo> readProcCpuInfo() {
    std::ifstream file("/proc/cpuinfo");
    std::vector<ProcCpuInfo> result;
    ProcCpuInfo current;
    bool active = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            if (active)
                result.push_back(std::move(current));
            current = {};
            active = false;
            continue;
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string key = trim(std::string_view(line).substr(0, colon));
        const std::string value = trim(std::string_view(line).substr(colon + 1));
        if (key == "processor") {
            current.id = static_cast<uint32_t>(std::stoul(value));
            active = true;
        } else if (key == "model name" || key == "Processor") {
            current.name = value;
        } else if (key == "flags" || key == "Features") {
            current.features = parseFeatures(value);
        }
    }
    if (active)
        result.push_back(std::move(current));
    return result;
}

inline void readCaches(const fs::path& cpuRoot, ProcessorInfo& processor) {
    const fs::path cacheRoot = cpuRoot / "cache";
    if (!fs::exists(cacheRoot))
        return;
    for (const auto& entry : fs::directory_iterator(cacheRoot)) {
        if (!entry.is_directory() || !entry.path().filename().string().starts_with("index"))
            continue;
        CacheInfo cache{
            .level = static_cast<uint8_t>(readUint(entry.path() / "level")),
            .type = parseCacheType(readText(entry.path() / "type")),
            .sizeBytes = parseSize(readText(entry.path() / "size")),
            .lineSizeBytes = static_cast<uint32_t>(readUint(entry.path() / "coherency_line_size")),
            .ways = static_cast<uint32_t>(readUint(entry.path() / "ways_of_associativity")),
            .sets = static_cast<uint32_t>(readUint(entry.path() / "number_of_sets")),
            .sharedLogicalCpus = parseCpuList(readText(entry.path() / "shared_cpu_list"))};
        std::ranges::sort(cache.sharedLogicalCpus);
        const bool known = std::ranges::any_of(processor.caches, [&](const CacheInfo& other) {
            return cache.level == other.level && cache.type == other.type &&
                   cache.sharedLogicalCpus == other.sharedLogicalCpus;
        });
        if (!known)
            processor.caches.push_back(std::move(cache));
    }
}

inline std::string driverName(const fs::path& device) {
    std::error_code ec;
    const fs::path driver = fs::read_symlink(device / "driver", ec);
    return ec ? std::string{} : driver.filename().string();
}

inline void enrichNvidia(GpuInfo& gpu) {
    if (gpu.driver != "nvidia")
        return;
    std::ifstream file(fs::path("/proc/driver/nvidia/gpus") / gpu.pciAddress / "information");
    std::string line;
    while (std::getline(file, line)) {
        const size_t colon = line.find(':');
        if (colon != std::string::npos && line.substr(0, colon) == "Model")
            gpu.name = trim(std::string_view(line).substr(colon + 1));
    }
}

inline uint32_t readHex(const fs::path& path) {
    const std::string value = readText(path);
    uint32_t result = 0;
    if (!value.starts_with("0x"))
        return 0;
    const auto [_, ec] = std::from_chars(value.data() + 2, value.data() + value.size(), result, 16);
    return ec == std::errc{} ? result : 0;
}

inline std::optional<PcieLink> readPcieLink(const fs::path& device) {
    const std::string speed = readText(device / "max_link_speed");
    const uint64_t lanes = readUint(device / "max_link_width");
    const double rate = std::strtod(speed.c_str(), nullptr);
    if (rate <= 0 || lanes == 0)
        return std::nullopt;
    uint32_t generation = 1;
    for (const double limit : {3.0, 6.0, 10.0, 20.0, 40.0})
        generation += rate > limit;
    return PcieLink{generation, static_cast<uint32_t>(lanes)};
}

// The device and the port above it: a card in a slower or narrower slot gets what the slot gives.
inline std::optional<PcieLink> pcieLink(const fs::path& device) {
    auto link = readPcieLink(device);
    if (const auto port = readPcieLink(device.parent_path()); link && port) {
        link->generation = std::min(link->generation, port->generation);
        link->lanes = std::min(link->lanes, port->lanes);
    }
    return link;
}

inline std::map<std::string, std::vector<std::string>> interfaceAddresses() {
    std::map<std::string, std::vector<std::string>> result;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0)
        return result;
    for (const ifaddrs* entry = list; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr)
            continue;
        char text[INET6_ADDRSTRLEN]{};
        const void* address = nullptr;
        if (entry->ifa_addr->sa_family == AF_INET)
            address = &reinterpret_cast<const sockaddr_in*>(entry->ifa_addr)->sin_addr;
        else if (entry->ifa_addr->sa_family == AF_INET6)
            address = &reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr)->sin6_addr;
        if (address && inet_ntop(entry->ifa_addr->sa_family, address, text, sizeof text))
            result[entry->ifa_name].push_back(text);
    }
    freeifaddrs(list);
    return result;
}

inline NetworkKind networkKind(const fs::path& path) {
    const uint64_t type = readUint(path / "type");
    if (type == ARPHRD_LOOPBACK)
        return NetworkKind::Loopback;
    // Only hardware has a device under it; the speed of the others is made up.
    if (!fs::exists(path / "device"))
        return NetworkKind::Virtual;
    if (fs::exists(path / "wireless"))
        return NetworkKind::Wireless;
    if (type == ARPHRD_INFINIBAND)
        return NetworkKind::Infiniband;
    return type == ARPHRD_ETHER ? NetworkKind::Ethernet : NetworkKind::Unknown;
}

inline std::string distributionName() {
    std::ifstream file("/etc/os-release");
    std::string prettyName, name, version, line;
    const auto unquote = [](std::string value) {
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
            value = value.substr(1, value.size() - 2);
        return value;
    };
    while (std::getline(file, line)) {
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = trim(std::string_view(line).substr(0, equals));
        const std::string value = unquote(trim(std::string_view(line).substr(equals + 1)));
        if (key == "PRETTY_NAME") prettyName = value;
        else if (key == "NAME") name = value;
        else if (key == "VERSION") version = value;
        else if (key == "VERSION_ID" && version.empty()) version = value;
    }
    if (!prettyName.empty()) return prettyName;
    if (!name.empty() && !version.empty()) return name + " " + version;
    return name.empty() ? std::string("Linux") : name;
}

}

inline std::vector<ProcessorInfo> processors() {
    using namespace Linux;
    std::vector<ProcessorInfo> processors;
    std::map<uint32_t, size_t> packageIndices;
    const fs::path root = "/sys/devices/system/cpu";

    for (const auto& entry : fs::directory_iterator(root)) {
        const std::string name = entry.path().filename().string();
        if (!entry.is_directory() || !name.starts_with("cpu") || name.size() <= 3)
            continue;
        uint32_t logicalCpu = 0;
        if (std::from_chars(name.data() + 3, name.data() + name.size(), logicalCpu).ec != std::errc{})
            continue;

        const fs::path topology = entry.path() / "topology";
        const auto packageId = static_cast<uint32_t>(readUint(topology / "physical_package_id"));
        const auto coreId = static_cast<uint32_t>(readUint(topology / "core_id"));

        auto [it, added] = packageIndices.try_emplace(packageId, processors.size());
        if (added)
            processors.push_back({.id = packageId, .arch = architecture()});
        ProcessorInfo& processor = processors[it->second];

        auto core = std::ranges::find(processor.cores, coreId, &CpuCoreInfo::id);
        if (core == processor.cores.end())
            processor.cores.push_back({.id = coreId, .logicalCpus = {logicalCpu}});
        else
            core->logicalCpus.push_back(logicalCpu);
    }

    const auto procInfo = readProcCpuInfo();
    for (ProcessorInfo& processor : processors) {
        if (processor.cores.empty() || processor.cores.front().logicalCpus.empty())
            continue;
        const uint32_t representative = processor.cores.front().logicalCpus.front();
        if (const auto info = std::ranges::find(procInfo, representative, &ProcCpuInfo::id); info != procInfo.end()) {
            processor.name = info->name;
            processor.features = info->features;
        }
        for (const CpuCoreInfo& core : processor.cores)
            for (uint32_t cpu : core.logicalCpus) {
                const fs::path cpuRoot = root / ("cpu" + std::to_string(cpu));
                const uint64_t minHz = readUint(cpuRoot / "cpufreq/cpuinfo_min_freq") * 1000ull;
                const uint64_t maxHz = readUint(cpuRoot / "cpufreq/cpuinfo_max_freq") * 1000ull;
                if (minHz && (!processor.minFrequencyHz || minHz < processor.minFrequencyHz))
                    processor.minFrequencyHz = minHz;
                processor.maxFrequencyHz = std::max(processor.maxFrequencyHz, maxHz);
                readCaches(cpuRoot, processor);
            }
    }
    return processors;
}

inline MemoryInfo memory() {
    using namespace Linux;
    MemoryInfo info;
    std::ifstream file("/proc/meminfo");
    std::string line;
    while (std::getline(file, line)) {
        if (line.starts_with("MemTotal:")) info.totalBytes = parseKbLine(line);
        else if (line.starts_with("MemAvailable:")) info.availableBytes = parseKbLine(line);
    }

    const fs::path root = "/sys/devices/system/node";
    if (!fs::exists(root))
        return info;
    for (const auto& entry : fs::directory_iterator(root)) {
        const std::string name = entry.path().filename().string();
        uint32_t nodeId = 0;
        if (!entry.is_directory() || !name.starts_with("node") || name.size() <= 4 ||
            std::from_chars(name.data() + 4, name.data() + name.size(), nodeId).ec != std::errc{})
            continue;
        MemoryNodeInfo node{.id = nodeId, .logicalCpus = parseCpuList(readText(entry.path() / "cpulist"))};
        std::ifstream nodeFile(entry.path() / "meminfo");
        while (std::getline(nodeFile, line)) {
            if (line.find("MemTotal:") != std::string::npos) node.totalBytes = parseKbLine(line);
            else if (line.find("MemFree:") != std::string::npos) node.freeBytes = parseKbLine(line);
        }
        info.nodes.push_back(std::move(node));
    }
    return info;
}

inline std::vector<GpuInfo> gpus() {
    using namespace Linux;
    std::vector<GpuInfo> result;
    const fs::path root = "/sys/class/drm";
    if (!fs::exists(root))
        return result;
    for (const auto& entry : fs::directory_iterator(root)) {
        const std::string card = entry.path().filename().string();
        if (!card.starts_with("card") || card.find('-') != std::string::npos)
            continue;
        const fs::path device = entry.path() / "device";
        std::error_code ec;
        const fs::path canonical = fs::canonical(device, ec);
        if (ec)
            continue;
        GpuInfo gpu{.name = readText(device / "vendor") + ":" + readText(device / "device"),
                    .driver = driverName(device),
                    .pciAddress = canonical.filename().string(),
                    .vendorId = readHex(device / "vendor"),
                    .deviceId = readHex(device / "device"),
                    .pcie = pcieLink(canonical)};
        if (gpu.driver == "amdgpu")
            gpu.vramBytes = readUint(device / "mem_info_vram_total");
        enrichNvidia(gpu);
        result.push_back(std::move(gpu));
    }
    return result;
}

inline std::vector<NetworkInfo> networks() {
    using namespace Linux;
    std::vector<NetworkInfo> result;
    const fs::path root = "/sys/class/net";
    if (!fs::exists(root))
        return result;
    auto addresses = interfaceAddresses();
    for (const auto& entry : fs::directory_iterator(root)) {
        NetworkInfo network{.name = entry.path().filename().string(),
                            .kind = networkKind(entry.path()),
                            .up = readText(entry.path() / "operstate") != "down"};
        // -1 and an error both mean the kernel does not know.
        const uint64_t speed = readUint(entry.path() / "speed");
        if (speed > 0 && network.kind != NetworkKind::Virtual && network.kind != NetworkKind::Loopback)
            network.megabits = speed;
        if (const auto found = addresses.find(network.name); found != addresses.end())
            network.addresses = std::move(found->second);
        result.push_back(std::move(network));
    }
    std::ranges::sort(result, {}, &NetworkInfo::name);
    return result;
}

inline std::optional<std::string> localAddressTo(const std::string& address) {
    sockaddr_storage target{};
    socklen_t size = sizeof(sockaddr_in);
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
    const int socket = ::socket(target.ss_family, SOCK_DGRAM, 0);
    if (socket < 0)
        return std::nullopt;
    sockaddr_storage local{};
    socklen_t localSize = sizeof local;
    const bool routed = connect(socket, reinterpret_cast<sockaddr*>(&target), size) == 0 &&
        getsockname(socket, reinterpret_cast<sockaddr*>(&local), &localSize) == 0;
    close(socket);
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
    OsInfo info{.name = Linux::distributionName()};
    utsname system{};
    if (uname(&system) == 0) {
        info.kernel = system.release;
        info.architecture = system.machine;
    }
    return info;
}

inline std::filesystem::path executable() {
    std::error_code error;
    return std::filesystem::read_symlink("/proc/self/exe", error);
}

}
