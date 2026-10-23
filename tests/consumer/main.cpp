#include <Lattice/Lattice.hpp>
#include <BmRunner/BigO.hpp>
#include <SysInfo/SystemInfo.hpp>

int main() {
    Lattice::Blueprints blueprints;
    const auto id = blueprints.add("Consumer::Component");

    Lattice::TreeView tree;
    Lattice::TableFormatter formatter;
    const auto build = Lattice::SystemInfo::collectBuild();
    const auto complexity = Lattice::Benchmarks::complexityName(
        Lattice::Benchmarks::Complexity::Linear
    );

    return id == Lattice::InvalidBlueprintId || tree.contains(0) ||
        build.executable.empty() || complexity.empty();
}
