#include <Lattice/kernel/PackageManager.hpp>
#include <Lattice/kernel/Requirements.hpp>
#include <lattice-logger/Logger.hpp>
#include <Lattice/kernel/PackageCandidate.hpp>
#include "lattice-logger/LogScope.hpp"

#include <algorithm>
#include <cstddef>
#include <format>

namespace Lattice {

    uint16_t PackageManager::load(std::filesystem::path path) {
        // загрузка внешних пакетов
        scanDirectory(path);
        checkCandidates();
        return loadCandidates();
    }

    namespace {
    // Package libraries: lib<Name>.so, not lib<Name>.tests.so / .bench.so.
    bool packageLibrary(const std::filesystem::path& file) {
        return file.extension() == DynamicLibrary::extension() && !file.stem().has_extension();
    }

    PackageManifest manifestOf(const Package& package) {
        PackageManifest manifest{
            .id = std::string(package.id),
            .name = std::string(package.name.empty() ? package.id : package.name),
            .version = package.version,
            .kernelApi = package.kernelApi
        };
        for (const auto& dependency : package.dependencyList()) {
            VersionRange range;
            range.min = dependency.min;
            range.max = dependency.before;
            range.requirement = dependency.before
                ? std::format(">={} <{}", dependency.min.str(), dependency.before->str())
                : std::format(">={}", dependency.min.str());
            manifest.dependencies.push_back({std::string(dependency.id), std::move(range)});
        }
        return manifest;
    }
    }

    // A directory is a package itself or contains packages one level below.
    void PackageManager::scanDirectory(std::filesystem::path path) {
        if (!std::filesystem::is_directory(path)) {
            Logger::warning(tag, "Package directory '{}' does not exist", path.string());
            return;
        }

        std::vector<std::filesystem::path> files;
        const auto collect = [&](const std::filesystem::path& directory) {
            for (const auto& entry : std::filesystem::directory_iterator(directory))
                if (entry.is_regular_file() && packageLibrary(entry.path()))
                    files.push_back(entry.path());
        };
        collect(path);
        for (const auto& entry : std::filesystem::directory_iterator(path))
            if (entry.is_directory())
                collect(entry.path());
        std::ranges::sort(files);

        for (const auto& file : files) {
            DynamicLibrary* library = dlLoader.loadLibrary(file);
            if (!library)
                continue;

            const auto* package = library->symbol<const Package*>(packageSymbol);
            if (!package) {
                Logger::warning(tag, "'{}' has no package metadata ({}); skipped", file.string(), packageSymbol);
                continue;
            }
            if (package->id.empty()) {
                Logger::error(tag, "'{}' declares a package without id", file.string());
                continue;
            }

            PackageCandidate candidate{file.parent_path(), manifestOf(*package)};
            candidate.library = library;
            auto [it, inserted] = candidates.emplace(candidate.manifest.id, std::move(candidate));
            if (!inserted) {
                Logger::error(tag, "Duplicate package id '{}': {}", package->id, file.string());
                continue;
            }

            Logger::info(tag, "Found package: {} v{}", package->id, package->version.str());
        }
    }

    void PackageManager::checkCandidates() {
        for (auto& [id, package] : candidates) {
            if (canLoad(id)) {
                prepareLoad(id);
            }
        }
    }

    bool PackageManager::canLoad(const std::string& id) {
        auto it = candidates.find(id);

        if (it == candidates.end()) {
            Logger::error("PackageManager", "PackageCandidate '{}' not found", id);
            return false;
        }

        PackageCandidate& package = it->second;

        if (package.status == LoadStatus::Valid ||
            package.status == LoadStatus::Loaded ||
            package.status == LoadStatus::Queued) {
            return true;
        }

        if (package.status == LoadStatus::DependencyCycle) {
            Logger::error("PackageManager", "Circular dependency detected at '{}'", id);
            return false;
        }

        if (package.status != LoadStatus::NonChecked)
            return false;

        package.status = LoadStatus::DependencyCycle;

        if (package.manifest.kernelApi.major != kernelApi.major ||
            package.manifest.kernelApi > kernelApi) {
            package.status = LoadStatus::IncompatibleVersion;
            Logger::error(tag, "PackageCandidate '{}' needs kernel API {}, host is {}",
                id, package.manifest.kernelApi.str(), kernelApi.str());
            return false;
        }

        for (const auto& dep : package.manifest.dependencies) {
            auto it = candidates.find(dep.id);
            if (it == candidates.end()) {
                package.status = LoadStatus::MissingDependency;
                Logger::error(tag, "Missing dependency '{}' for package '{}'", dep.id, id);
                return false;
            }

            PackageCandidate& dependency = it->second;
            if (!dep.requirement.contains(dependency.manifest.version)) {
                package.status = LoadStatus::IncompatibleVersion;
                Logger::error(tag, "PackageCandidate '{}' requires '{}' version {}, but found v{}", 
                    id, dep.id, dep.requirement.requirement, dependency.manifest.version.str());
                return false;
            }

            if (!canLoad(dep.id)) {
                package.status = it->second.status;
                Logger::error(tag, "Dependency '{}' cannot be loaded for package '{}'", dep.id, id);
                return false;
            }
        }

        package.status = LoadStatus::Valid;
        Logger::ok(tag, "Dependency check passed: {}", id);
        return true;
    }

    bool PackageManager::prepareLoad(const std::string& id) {
        auto it = candidates.find(id);

        if (it == candidates.end())
            return false;

        PackageCandidate& package = it->second;

        if (package.status == LoadStatus::Queued ||
            package.status == LoadStatus::Loaded) {
            return true;
        }

        for (const auto& dep : package.manifest.dependencies) {
            if (!prepareLoad(dep.id))
                return false;
        }

        package.status = LoadStatus::Queued;
        loadQueue.push_back(&package);

        return true;
    }

    uint16_t PackageManager::loadCandidates() {
        uint16_t loadedPackages = 0; 
        for (PackageCandidate* candidate : loadQueue) {
            if (candidate->status == LoadStatus::Loaded) continue;
            bool dependenciesLoaded = true;
            for (const auto& dep : candidate->manifest.dependencies) {
                auto found = candidates.find(dep.id);
                if (found == candidates.end() || found->second.status != LoadStatus::Loaded)
                    dependenciesLoaded = false;
            }
            if (!dependenciesLoaded) {
                candidate->status = LoadStatus::Failed;
                continue;
            }
            LogScope scope(tag, "Loading '{}'", candidate->path.string());
            if (loadPackage(candidate)) {
                scope.finish("Loaded package '{}'", candidate->manifest.id);
                loadedPackages++;
            } else {
                scope.finishError("failed to load package '{}'", candidate->manifest.id);
            }
        }
        if (!loadedPackages) {
            Logger::warning(tag, "No packages could be loaded");
        } else {
            Logger::info(tag, "Loaded packages: {}", loadedPackages);
        }
        return loadedPackages;
    }

    bool PackageManager::loadPackage(PackageCandidate* candidate) {
        // The library was opened while reading its metadata.
        const auto path = candidate->library ? candidate->library->path : candidate->path;
        if (!candidate->library) {
            candidate->status = LoadStatus::Failed;
            return false;
        }

        const size_t depsBefore = compileDepSink().size();

        auto compose = candidate->library->symbol<PackageComposeFn>("compose");
        if (!compose) {
            Logger::error(tag, "compose symbol not found in '{}'", path.string());
            candidate->status = LoadStatus::Failed;
            return false;
        }

        const auto before = blueprints.size();
        auto snapshot = blueprints;

        try {
            // configure is optional: a package without live wiring exports only compose.
            blueprints.addComposition(candidate->manifest.id, compose,
                candidate->library->symbol<PackageConfigureFn>("configure"));
        } catch (const std::exception& error) {
            blueprints = std::move(snapshot);
            Logger::error(tag, "Registration failed for '{}': {}", candidate->manifest.id, error.what());
            candidate->status = LoadStatus::Failed;
            return false;
        } catch (...) {
            blueprints = std::move(snapshot);
            candidate->status = LoadStatus::Failed;
            return false;
        }
        const auto after = blueprints.size();

        PackageCatalog catalog{.packageId = candidate->manifest.id};

        const auto& sink = compileDepSink();
        catalog.deps.assign(sink.begin() + depsBefore, sink.end());

        for (BlueprintId id = before; id < after; ++id)
            catalog.provided.push_back(id);

        if (after == before)
            Logger::warning(tag, "PackageCandidate '{}' does not provide anything", candidate->manifest.id);

        recordPackageCatalog(std::move(catalog));

        candidate->status = LoadStatus::Loaded;
        return true;
    }

    const PackageCandidate* PackageManager::findCandidate(std::string_view id) const {
        auto it = candidates.find(std::string(id));

        if (it == candidates.end())
            return nullptr;

        return &it->second;
    }

}
