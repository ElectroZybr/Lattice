#pragma once

#include <filesystem>
#include <unordered_map>
#include <vector>

#include <Lattice/kernel/DLLoader.hpp>
#include <Lattice/kernel/PackageCandidate.hpp>
#include <Lattice/kernel/Visibility.hpp>

namespace Lattice {

struct PackageManifest;

class LT_API PackageManager {
    static constexpr std::string_view tag = "PackageManager";
public:
    PackageManager(Blueprints& blueprints, DLLoader& dlLoader)
        : blueprints(blueprints), dlLoader(dlLoader) {}

    uint16_t load(std::filesystem::path path);
    
    void scanDirectory(std::filesystem::path path);
    void checkCandidates();
    uint16_t loadCandidates();

    const PackageCandidate* findCandidate(std::string_view id) const;
    const std::vector<PackageCandidate*>& queue() const { return loadQueue; }

private:
    bool canLoad(const std::string& id);
    bool prepareLoad(const std::string& id);
    bool loadPackage(PackageCandidate* candidate);

    std::unordered_map<std::string, PackageCandidate> candidates;
    std::vector<PackageCandidate*> loadQueue;

    Blueprints& blueprints;
    DLLoader& dlLoader;
};
}
