#pragma once

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <lattice-sysinfo/Visibility.hpp>

namespace Lattice::SystemInfo {

enum class CpuArch : uint8_t {
    Unknown,
    X86_64,
    AArch64
};

enum class Compiler : uint8_t {
    Unknown,
    GCC,
    Clang,
    MSVC
};

enum class BuildType : uint8_t {
    Unknown,
    Debug,
    Release
};

enum class CacheType : uint8_t {
    Data,
    Instruction,
    Unified
};

enum class CpuFeature : uint64_t {
    None   = 0,

    SSE    = 1ull << 0,
    SSE2   = 1ull << 1,
    SSE3   = 1ull << 2,
    SSSE3  = 1ull << 3,
    SSE41  = 1ull << 4,
    SSE42  = 1ull << 5,

    AVX    = 1ull << 6,
    AVX2   = 1ull << 7,
    FMA    = 1ull << 8,

    AVX512F = 1ull << 9,

    NEON   = 1ull << 10,
    SVE    = 1ull << 11
};

constexpr CpuFeature operator|(CpuFeature a, CpuFeature b) noexcept {
    return static_cast<CpuFeature>(
        static_cast<uint64_t>(a) |
        static_cast<uint64_t>(b)
    );
}

constexpr CpuFeature operator&(CpuFeature a, CpuFeature b) noexcept {
    return static_cast<CpuFeature>(
        static_cast<uint64_t>(a) &
        static_cast<uint64_t>(b)
    );
}

constexpr CpuFeature& operator|=(CpuFeature& a, CpuFeature b) noexcept {
    a = a | b;
    return a;
}

constexpr bool hasFeature(CpuFeature features, CpuFeature feature) noexcept {
    return (features & feature) == feature;
}

struct BuildInfo {
    std::string executable;

    uint32_t versionMajor = 0;
    uint32_t versionMinor = 0;
    uint32_t versionPatch = 0;

    Compiler compiler = Compiler::Unknown;

    uint32_t compilerMajor = 0;
    uint32_t compilerMinor = 0;
    uint32_t compilerPatch = 0;

    BuildType build = BuildType::Unknown;
};

struct OsInfo {
    std::string name;
    std::string kernel;
    std::string architecture;
};

struct CacheInfo {
    uint8_t level = 0;
    CacheType type = CacheType::Unified;

    uint64_t sizeBytes = 0;
    uint32_t lineSizeBytes = 0;
    uint32_t ways = 0;
    uint32_t sets = 0;

    std::vector<uint32_t> sharedLogicalCpus;
};

struct CpuCoreInfo {
    uint32_t id = 0;
    std::vector<uint32_t> logicalCpus;
};

struct ProcessorInfo {
    uint32_t id = 0;

    std::string name;
    CpuArch arch = CpuArch::Unknown;
    CpuFeature features = CpuFeature::None;

    uint64_t minFrequencyHz = 0;
    uint64_t maxFrequencyHz = 0;

    std::vector<CpuCoreInfo> cores;
    std::vector<CacheInfo> caches;
};

struct MemoryNodeInfo {
    uint32_t id = 0;
    uint64_t totalBytes = 0;
    uint64_t freeBytes = 0;
    std::vector<uint32_t> logicalCpus;
};

struct MemoryInfo {
    uint64_t totalBytes = 0;
    uint64_t availableBytes = 0;

    std::vector<MemoryNodeInfo> nodes;
};

/// What a PCIe link can carry: the lesser of the device and its slot. The speed in use is not
/// it: a resting GPU drops it.
struct PcieLink {
    uint32_t generation = 0;
    uint32_t lanes = 0;

    /// Bytes per second each way after the line code, before the protocol's own overhead.
    double bytesPerSecond() const noexcept {
        static constexpr double transfers[] = {2.5e9, 5e9, 8e9, 16e9, 32e9, 64e9};
        if (generation == 0 || generation > std::size(transfers))
            return 0;
        const double code = generation <= 2 ? 8.0 / 10.0 : 128.0 / 130.0;
        return transfers[generation - 1] * code / 8.0 * lanes;
    }
};

struct GpuInfo {
    std::string name;
    std::string driver;
    std::string pciAddress;
    std::string computeInfo;

    uint64_t vramBytes = 0;

    /// PCI ids, 0 when unknown: the same a graphics API reports for its adapter.
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    std::optional<PcieLink> pcie;
};

enum class NetworkKind : uint8_t {
    Unknown,
    Ethernet,
    Wireless,
    Infiniband,
    /// No hardware under it: VPN, bridge, container.
    Virtual,
    Loopback
};

struct NetworkInfo {
    std::string name;
    NetworkKind kind = NetworkKind::Unknown;
    bool up = false;
    /// Nominal speed of the link; none when the system does not say or the interface is virtual.
    std::optional<uint64_t> megabits;
    /// Numeric IPv4 and IPv6.
    std::vector<std::string> addresses;
};

struct MachineInfo {
    OsInfo os;
    BuildInfo build;

    MemoryInfo memory;
    std::vector<ProcessorInfo> processors;
    std::vector<GpuInfo> gpus;
    std::vector<NetworkInfo> networks;
};

LT_SYSINFO std::vector<ProcessorInfo> collectCPU();
LT_SYSINFO std::vector<GpuInfo> collectGpus();
/// Interfaces change while a program runs: collect anew when it matters.
LT_SYSINFO std::vector<NetworkInfo> collectNetworks();
/// The local address the system would send from to this numeric address: which interface
/// reaches it. Nothing is sent. None for an address that is not numeric or has no route.
LT_SYSINFO std::optional<std::string> localAddressTo(const std::string& address);
LT_SYSINFO MemoryInfo collectMemory();
LT_SYSINFO OsInfo collectOS();
/// The version of the program: it knows it, this library does not. collectBuild reports it.
LT_SYSINFO void setProgramVersion(std::string_view version);
LT_SYSINFO BuildInfo collectBuild();

inline MachineInfo collectMachineInfo() {
    MachineInfo info;

    info.os = collectOS();
    info.build = collectBuild();
    info.processors = collectCPU();
    info.memory = collectMemory();
    info.gpus = collectGpus();
    info.networks = collectNetworks();

    return info;
}

}
