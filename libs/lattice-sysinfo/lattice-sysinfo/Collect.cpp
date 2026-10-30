#include "SystemInfo.hpp"
#include "platform/Platform.hpp"

namespace Lattice::SystemInfo {

std::vector<ProcessorInfo> collectCPU() { return Platform::processors(); }

std::vector<GpuInfo> collectGpus() { return Platform::gpus(); }

MemoryInfo collectMemory() { return Platform::memory(); }

OsInfo collectOS() {
    OsInfo info = Platform::os();
    if (info.architecture.empty())
        switch (Platform::architecture()) {
            case CpuArch::X86_64: info.architecture = "x86_64"; break;
            case CpuArch::AArch64: info.architecture = "aarch64"; break;
            case CpuArch::Unknown: info.architecture = "unknown"; break;
        }
    return info;
}

}
