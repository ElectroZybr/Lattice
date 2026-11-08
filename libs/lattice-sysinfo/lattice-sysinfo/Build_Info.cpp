#include "SystemInfo.hpp"

#include <charconv>
#include <string>
#include <string_view>

#include "platform/Platform.hpp"

#ifndef BUILD_VERSION
#define BUILD_VERSION "0.0.0"
#endif

namespace Lattice::SystemInfo {

namespace {

void parseVersion(std::string_view version, BuildInfo& info) {
    uint32_t* parts[] = {
        &info.versionMajor,
        &info.versionMinor,
        &info.versionPatch
    };

    for (uint32_t* part : parts) {
        const size_t dot = version.find('.');
        const std::string_view value = version.substr(0, dot);

        std::from_chars(value.data(), value.data() + value.size(), *part);

        if (dot == std::string_view::npos)
            break;

        version.remove_prefix(dot + 1);
    }
}

}

static std::string& programVersion() {
    static std::string version = BUILD_VERSION;
    return version;
}

void setProgramVersion(std::string_view version) { programVersion() = version; }

BuildInfo collectBuild() {
    BuildInfo info;
    const auto executable = Platform::executable();
    info.executable = executable.empty() ? "unknown" : executable.filename().string();
    parseVersion(programVersion(), info);

#if defined(__clang__)
    info.compiler = Compiler::Clang;
    info.compilerMajor = __clang_major__;
    info.compilerMinor = __clang_minor__;
    info.compilerPatch = __clang_patchlevel__;
#elif defined(__GNUC__)
    info.compiler = Compiler::GCC;
    info.compilerMajor = __GNUC__;
    info.compilerMinor = __GNUC_MINOR__;
    info.compilerPatch = __GNUC_PATCHLEVEL__;
#elif defined(_MSC_VER)
    info.compiler = Compiler::MSVC;
    info.compilerMajor = _MSC_VER / 100;
    info.compilerMinor = _MSC_VER % 100;
#endif

#if defined(NDEBUG)
    info.build = BuildType::Release;
#else
    info.build = BuildType::Debug;
#endif

    return info;
}

}
