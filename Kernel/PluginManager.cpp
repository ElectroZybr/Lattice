#include <Lattice/Kernel/PluginManager.hpp>
#include <Lattice/Kernel/Requirements.hpp>
#include <Logger/Logger.hpp>
#include <Lattice/Kernel/Plugin.hpp>
#include "Logger/LogScope.hpp"

#include <algorithm>
#include <cstddef>
#include <format>

namespace Lattice {

    uint16_t PluginManager::load(std::filesystem::path path) {
        // загрузка внешних плагинов
        scanDirectory(path);
        checkCandidates();
        return loadCandidates();
    }

    namespace {
    // Package libraries: lib<Name>.so, not lib<Name>.tests.so / .bench.so.
    bool packageLibrary(const std::filesystem::path& file) {
        return file.extension() == DynamicLibrary::extension() && !file.stem().has_extension();
    }

    PluginManifest manifestOf(const Package& package) {
        PluginManifest manifest{
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
    void PluginManager::scanDirectory(std::filesystem::path path) {
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

            Plugin candidate{file.parent_path(), manifestOf(*package)};
            candidate.library = library;
            auto [it, inserted] = candidates.emplace(candidate.manifest.id, std::move(candidate));
            if (!inserted) {
                Logger::error(tag, "Duplicate package id '{}': {}", package->id, file.string());
                continue;
            }

            Logger::info(tag, "Found package: {} v{}", package->id, package->version.str());
        }
    }

    void PluginManager::checkCandidates() {
        for (auto& [id, plugin] : candidates) {
            if (canLoad(id)) {
                prepareLoad(id);
            }
        }
    }

    bool PluginManager::canLoad(const std::string& id) {
        auto it = candidates.find(id);

        if (it == candidates.end()) {
            Logger::error("PluginManager", "Plugin '{}' not found", id);
            return false;
        }

        Plugin& plugin = it->second;

        if (plugin.status == LoadStatus::Valid ||
            plugin.status == LoadStatus::Loaded ||
            plugin.status == LoadStatus::Queued) {
            return true;
        }

        if (plugin.status == LoadStatus::DependencyCycle) {
            Logger::error("PluginManager", "Circular dependency detected at '{}'", id);
            return false;
        }

        if (plugin.status != LoadStatus::NonChecked)
            return false;

        plugin.status = LoadStatus::DependencyCycle;

        if (plugin.manifest.kernelApi.major != kernelApi.major ||
            plugin.manifest.kernelApi > kernelApi) {
            plugin.status = LoadStatus::IncompatibleVersion;
            Logger::error(tag, "Plugin '{}' needs kernel API {}, host is {}",
                id, plugin.manifest.kernelApi.str(), kernelApi.str());
            return false;
        }

        for (const auto& dep : plugin.manifest.dependencies) {
            auto it = candidates.find(dep.id);
            if (it == candidates.end()) {
                plugin.status = LoadStatus::MissingDependency;
                Logger::error(tag, "Missing dependency '{}' for plugin '{}'", dep.id, id);
                return false;
            }

            Plugin& dependency = it->second;
            if (!dep.requirement.contains(dependency.manifest.version)) {
                plugin.status = LoadStatus::IncompatibleVersion;
                Logger::error(tag, "Plugin '{}' requires '{}' version {}, but found v{}", 
                    id, dep.id, dep.requirement.requirement, dependency.manifest.version.str());
                return false;
            }

            if (!canLoad(dep.id)) {
                plugin.status = it->second.status;
                Logger::error(tag, "Dependency '{}' cannot be loaded for plugin '{}'", dep.id, id);
                return false;
            }
        }

        plugin.status = LoadStatus::Valid;
        Logger::ok(tag, "Dependency check passed: {}", id);
        return true;
    }

    bool PluginManager::prepareLoad(const std::string& id) {
        auto it = candidates.find(id);

        if (it == candidates.end())
            return false;

        Plugin& plugin = it->second;

        if (plugin.status == LoadStatus::Queued ||
            plugin.status == LoadStatus::Loaded) {
            return true;
        }

        for (const auto& dep : plugin.manifest.dependencies) {
            if (!prepareLoad(dep.id))
                return false;
        }

        plugin.status = LoadStatus::Queued;
        loadQueue.push_back(&plugin);

        return true;
    }

    uint16_t PluginManager::loadCandidates() {
        uint16_t loadedPlugins = 0; 
        for (Plugin* candidate : loadQueue) {
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
            if (loadPlugin(candidate)) {
                scope.finish("Loaded plugin '{}'", candidate->manifest.id);
                loadedPlugins++;
            } else {
                scope.finishError("failed to load plugin '{}'", candidate->manifest.id);
            }
        }
        if (!loadedPlugins) {
            Logger::warning(tag, "No plugins could be loaded");
        } else {
            Logger::info(tag, "Loaded plugins: {}", loadedPlugins);
        }
        return loadedPlugins;
    }

    bool PluginManager::loadPlugin(Plugin* candidate) {
        // The library was opened while reading its metadata.
        const auto path = candidate->library ? candidate->library->path : candidate->path;
        if (!candidate->library) {
            candidate->status = LoadStatus::Failed;
            return false;
        }

        const size_t depsBefore = compileDepSink().size();

        auto compose = candidate->library->symbol<PluginComposeFn>("compose");
        if (!compose) {
            Logger::error(tag, "compose symbol not found in '{}'", path.string());
            candidate->status = LoadStatus::Failed;
            return false;
        }

        const auto before = blueprints.size();
        auto snapshot = blueprints;

        try {
            blueprints.addComposition(candidate->manifest.id, compose);
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

        PluginCatalog catalog{.pluginId = candidate->manifest.id};

        const auto& sink = compileDepSink();
        catalog.deps.assign(sink.begin() + depsBefore, sink.end());

        for (BlueprintId id = before; id < after; ++id)
            catalog.provided.push_back(id);

        if (after == before)
            Logger::warning(tag, "Plugin '{}' does not provide anything", candidate->manifest.id);

        recordPluginCatalog(std::move(catalog));

        candidate->status = LoadStatus::Loaded;
        return true;
    }

    const Plugin* PluginManager::findCandidate(std::string_view id) const {
        auto it = candidates.find(std::string(id));

        if (it == candidates.end())
            return nullptr;

        return &it->second;
    }

}
