#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Lattice/kernel/Blueprints.hpp>
#include <Lattice/kernel/DynamicLibrary.hpp>
#include <Lattice/kernel/Package.hpp>

namespace Lattice {
    
struct VersionRange {
    std::optional<Version> min;
    std::optional<Version> max;
    std::string requirement;

    bool contains(const Version& version) const {
        if (min && version < *min)
            return false;

        if (max && version >= *max)
            return false;

        return true;
    }

    static Version parseVersion(std::string_view str) {
        Version version{};
        const char* it = str.data();
        const char* const end = it + str.size();
        for (uint8_t* part : {&version.major, &version.minor, &version.patch}) {
            const auto [next, error] = std::from_chars(it, end, *part);
            if (error != std::errc{} || next == end || *next != '.')
                break;
            it = next + 1;
        }

        return version;
    }

    static VersionRange parse(std::string_view str) {
        VersionRange range;

        range.requirement = str;

        if (str.starts_with(">=")) {
            range.min = parseVersion(str.substr(2));
        }
        else if (str.starts_with("<")) {
            range.max = parseVersion(str.substr(1));
        }
        else {
            // точная версия
            Version v = parseVersion(str);
            range.min = v;

            Version next = v;
            next.patch++;

            range.max = next;
        }

        return range;
    }
};

struct PackageRequirement {
    std::string id;
    VersionRange requirement;
};

struct PackageManifest {
    std::string id;
    std::string name;
    Version version;
    Version kernelApi{};

    std::vector<PackageRequirement> dependencies;
    /// Names point into the package library: valid while it is loaded.
    std::vector<Implementation> implementations;
};

enum class LoadStatus {
    NonChecked,
    // dependency check
    Valid,
    MissingDependency,
    IncompatibleVersion,
    DependencyCycle,
    // loading
    Queued,
    Loaded,
    Failed
};

using PackageComposeFn = void(*)(::NodeBuild);
using PackageConfigureFn = void(*)(::NodeConfigure);

struct PackageCandidate {
    std::filesystem::path path;
    PackageManifest manifest;
    LoadStatus status = LoadStatus::NonChecked;
    DynamicLibrary* library = nullptr;
    std::vector<std::string> providedAPIs;
};
}