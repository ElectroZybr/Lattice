// Linux: perf_event_open(2), the counter page mapped for RDPMC, uncore PMUs from sysfs; heap
// statistics of glibc.

#include "../Heap.hpp"
#include "../PerfEvent.hpp"

#include <cerrno>
#include <cstring>
#include <format>
#include <fstream>
#include <string>

#include <linux/perf_event.h>
#include <malloc.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

#include <lattice-tools/Exception.hpp>

namespace Lattice::Benchmarks {

namespace {

constexpr std::string_view ParanoidHint = "; try 'sudo sysctl -w kernel.perf_event_paranoid=-1'";

#if defined(__x86_64__) || defined(__i386__)

inline uint64_t rdpmc(uint32_t counter) noexcept {
    uint32_t lo;
    uint32_t hi;
    asm volatile("lfence\n\t"
                 "rdpmc\n\t"
                 "lfence"
                 : "=a"(lo), "=d"(hi)
                 : "c"(counter)
                 : "memory");
    return (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
}

inline uint64_t rdtsc() noexcept {
    uint32_t lo;
    uint32_t hi;
    asm volatile("lfence\n\t"
                 "rdtsc\n\t"
                 "lfence"
                 : "=a"(lo), "=d"(hi)
                 :
                 : "memory");
    return (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
}

#endif

/// AMD Zen family 17h-1Ah: its core PMU has the model-specific LLC proxies
/// below.
bool amdZen() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int eax = 0;
    unsigned int ebx = 0;
    unsigned int ecx = 0;
    unsigned int edx = 0;
    if (!__get_cpuid(0, &eax, &ebx, &ecx, &edx)) return false;

    char vendor[13]{};
    std::memcpy(vendor, &ebx, sizeof(ebx));
    std::memcpy(vendor + 4, &edx, sizeof(edx));
    std::memcpy(vendor + 8, &ecx, sizeof(ecx));
    if (std::string_view(vendor) != "AuthenticAMD") return false;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return false;

    const unsigned int baseFamily = (eax >> 8) & 0x0f;
    const unsigned int extendedFamily = (eax >> 20) & 0xff;
    const unsigned int family = baseFamily == 0x0f ? baseFamily + extendedFamily : baseFamily;
    return family >= 0x17 && family <= 0x1a;
#else
    return false;
#endif
}

constexpr uint64_t rawConfig(uint64_t event, uint64_t mask) noexcept { return event | (mask << 8); }

// AMD Zen core PMU proxies for per-thread data LLC traffic:
// - L2 misses caused by L1D misses become L3 accesses;
// - L1D fills sourced from local/remote memory or IO missed the L3.
constexpr uint64_t AmdZenLlcReads = rawConfig(0x64, 0x08);
constexpr uint64_t AmdZenLlcMisses = rawConfig(0x44, 0x48);

constexpr uint64_t cacheConfig(uint64_t cache, uint64_t operation, uint64_t result) noexcept {
    return cache | (operation << 8) | (result << 16);
}

perf_event_attr attributes(uint32_t type, uint64_t config) {
    perf_event_attr attr{};
    attr.size = sizeof(attr);
    attr.type = type;
    attr.config = config;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    return attr;
}

std::string readLine(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    file >> line;
    return line;
}

PerfEvent::Snapshot snapshotRdpmc(const perf_event_mmap_page* page_) noexcept {
#if defined(__x86_64__) || defined(__i386__)

    PerfEvent::Snapshot result{};
    uint32_t index = 0;
    uint64_t cycles = 0;
    uint64_t timeOffset = 0;
    uint64_t timeCycles = 0;
    uint64_t timeMask = 0;
    uint32_t timeMult = 0;
    uint16_t timeShift = 0;
    bool userTime = false;
    bool userTimeShort = false;

    uint32_t sequence = 0;

    while (true) {
        sequence = __atomic_load_n(&page_->lock, __ATOMIC_ACQUIRE);

        if (sequence & 1) continue;

        index = page_->index;

        const int64_t offset = page_->offset;

        const uint16_t width = page_->pmc_width;

        result.timeEnabled = page_->time_enabled;

        result.timeRunning = page_->time_running;

        userTime = page_->cap_user_time && result.timeEnabled != result.timeRunning;

        if (userTime) {
            cycles = rdtsc();
            timeOffset = page_->time_offset;
            timeMult = page_->time_mult;
            timeShift = page_->time_shift;
            userTimeShort = page_->cap_user_time_short;
            timeCycles = page_->time_cycles;
            timeMask = page_->time_mask;
        }

        int64_t value = offset;

        if (index) {
            int64_t pmc = static_cast<int64_t>(rdpmc(index - 1));

            if (width && width < 64) {
                pmc <<= 64 - width;
                pmc >>= 64 - width;
            }

            value += pmc;
        }

        result.value = static_cast<uint64_t>(value);

        __atomic_thread_fence(__ATOMIC_ACQUIRE);

        if (__atomic_load_n(&page_->lock, __ATOMIC_RELAXED) == sequence) { break; }
    }

    if (userTime) {
        if (userTimeShort) { cycles = timeCycles + ((cycles - timeCycles) & timeMask); }

        const uint64_t quotient = cycles >> timeShift;

        const uint64_t remainderMask = timeShift == 0 ? 0 : (uint64_t{1} << timeShift) - 1;

        const uint64_t remainder = cycles & remainderMask;

        const uint64_t delta = timeOffset + quotient * timeMult + ((remainder * timeMult) >> timeShift);

        result.timeEnabled += delta;

        if (index) result.timeRunning += delta;
    }

    return result;

#else
    return {};
#endif
}

PerfEvent::Snapshot snapshotRead(int fd_) {
    struct ReadData {
        uint64_t value;
        uint64_t timeEnabled;
        uint64_t timeRunning;
    } data{};

    const ssize_t bytes = ::read(fd_, &data, sizeof(data));

    if (bytes != static_cast<ssize_t>(sizeof(data))) {
        throw Exception<PerfEvent>("Failed to read perf event: {}", std::strerror(errno));
    }

    return {.value = data.value, .timeEnabled = data.timeEnabled, .timeRunning = data.timeRunning};
}

}

struct PerfEvent::Access {
    static PerfEvent open(perf_event_attr attr, int pid, int cpu, int group) {
        const int fd = static_cast<int>(::syscall(SYS_perf_event_open, &attr, pid, cpu, group, 0UL));
        if (fd < 0)
            return PerfEvent(-1, nullptr,
                             std::format("perf_event_open failed: {}{}", std::strerror(errno), ParanoidHint));
        return PerfEvent(fd, map(fd), {});
    }

    static void* map(int fd) {
#if defined(__x86_64__) || defined(__i386__)
        const long pageSize = ::sysconf(_SC_PAGESIZE);
        if (pageSize <= 0) return nullptr;
        void* mapping = ::mmap(nullptr, static_cast<size_t>(pageSize), PROT_READ, MAP_SHARED, fd, 0);
        return mapping == MAP_FAILED ? nullptr : mapping;
#else
        return nullptr;
#endif
    }
};

PerfEvent PerfEvent::hardware(HardwareCounter counter, const PerfEvent* leader) {
    static constexpr uint64_t Configs[] = {PERF_COUNT_HW_CPU_CYCLES,          PERF_COUNT_HW_INSTRUCTIONS,
                                           PERF_COUNT_HW_CACHE_REFERENCES,    PERF_COUNT_HW_CACHE_MISSES,
                                           PERF_COUNT_HW_BRANCH_INSTRUCTIONS, PERF_COUNT_HW_BRANCH_MISSES};
    perf_event_attr attr = attributes(PERF_TYPE_HARDWARE, Configs[static_cast<size_t>(counter)]);
    attr.disabled = leader ? 0 : 1;
    return Access::open(attr, 0, -1, leader ? leader->handle_ : -1);
}

PerfEvent PerfEvent::cache(CacheCounter counter, bool modelSpecific) {
    if (modelSpecific) {
        const bool llc = counter == CacheCounter::LlcReads || counter == CacheCounter::LlcMisses;
        if (!llc || !amdZen()) return PerfEvent(-1, nullptr, "no model-specific event for this counter on this CPU");
        return Access::open(
            attributes(PERF_TYPE_RAW, counter == CacheCounter::LlcReads ? AmdZenLlcReads : AmdZenLlcMisses), 0, -1, -1);
    }
    static constexpr uint64_t Configs[] = {
        cacheConfig(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS),
        cacheConfig(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS),
        cacheConfig(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS),
        cacheConfig(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS),
        cacheConfig(PERF_COUNT_HW_CACHE_DTLB, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS)};
    return Access::open(attributes(PERF_TYPE_HW_CACHE, Configs[static_cast<size_t>(counter)]), 0, -1, -1);
}

PerfEvent PerfEvent::software(SoftwareCounter counter) {
    perf_event_attr attr =
        attributes(PERF_TYPE_SOFTWARE, counter == SoftwareCounter::MinorFaults ? PERF_COUNT_SW_PAGE_FAULTS_MIN
                                                                               : PERF_COUNT_SW_PAGE_FAULTS_MAJ);
    attr.exclude_kernel = 0;
    return Access::open(attr, 0, -1, -1);
}

PerfEvent PerfEvent::uncore(std::string_view pmu, uint64_t config) {
    const std::string root = std::format("/sys/bus/event_source/devices/{}/", pmu);
    const std::string type = readLine(root + "type");
    const std::string mask = readLine(root + "cpumask");
    if (type.empty() || mask.empty()) {
        std::string reason = std::format("{} PMU is missing", pmu);
        if (pmu == "amd_df")
            reason += "; run 'sudo modprobe amd_uncore' and add amd_uncore to "
                      "/etc/modules-load.d/amd-uncore.conf";
        return PerfEvent(-1, nullptr, std::move(reason));
    }
    perf_event_attr attr = attributes(static_cast<uint32_t>(std::stoul(type)), config);
    attr.exclude_kernel = 0;
    attr.exclude_hv = 0;
    return Access::open(attr, -1, std::stoi(mask), -1);
}

bool PerfEvent::fastReadable() const noexcept {
#if defined(__x86_64__) || defined(__i386__)
    return page_ && static_cast<const perf_event_mmap_page*>(page_)->cap_user_rdpmc;
#else
    return false;
#endif
}

namespace {

void control(int fd, unsigned long request, bool group, std::string_view action) {
    if (::ioctl(fd, request, group ? PERF_IOC_FLAG_GROUP : 0) == -1)
        throw Exception<PerfEvent>("Failed to {} perf event: {}", action, std::strerror(errno));
}

}

void PerfEvent::reset(bool group) {
    if (available()) control(handle_, PERF_EVENT_IOC_RESET, group, "reset");
}

void PerfEvent::enable(bool group) {
    if (available()) control(handle_, PERF_EVENT_IOC_ENABLE, group, "enable");
}

void PerfEvent::disable(bool group) {
    if (available()) control(handle_, PERF_EVENT_IOC_DISABLE, group, "disable");
}

PerfEvent::Snapshot PerfEvent::snapshot() const {
    if (!available()) return {};
#if defined(__x86_64__) || defined(__i386__)
    if (fastReadable()) return snapshotRdpmc(static_cast<const perf_event_mmap_page*>(page_));
#endif
    return snapshotRead(handle_);
}

void PerfEvent::close() noexcept {
    if (page_) {
        const long pageSize = ::sysconf(_SC_PAGESIZE);
        if (pageSize > 0) ::munmap(page_, static_cast<size_t>(pageSize));
        page_ = nullptr;
    }
    if (handle_ >= 0) {
        ::close(handle_);
        handle_ = -1;
    }
}

std::optional<size_t> Heap::usedBytes() noexcept {
    const auto info = mallinfo2();
    return info.uordblks + info.hblkhd;
}

std::string_view Heap::unavailableReason() const noexcept { return {}; }

}
