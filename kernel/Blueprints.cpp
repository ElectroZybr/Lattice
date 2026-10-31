#include <Lattice/kernel/Blueprints.hpp>

#include <algorithm>
#include <format>
#include <lattice-tools/Exception.hpp>
#include <lattice-logger/Logger.hpp>
#include <lattice-tools/TreeFormatter.hpp>

namespace Lattice {

namespace {

// The start of the last name in `text`: after '<', ',' or ' ' outside parentheses, so clang's
// "(anonymous namespace)" stays one name.
size_t lastNameStart(std::string_view text) {
    int depth = 0;
    for (size_t i = text.size(); i-- > 0;) {
        if (text[i] == ')')
            ++depth;
        else if (text[i] == '(')
            --depth;
        else if (depth == 0 && (text[i] == '<' || text[i] == ',' || text[i] == ' '))
            return i + 1;
    }
    return 0;
}

}

std::string Blueprint::shortName() const {
    std::string result;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name.compare(i, 2, "::") == 0) {
            // Drop the qualifier just copied: back to the start of this name.
            result.erase(lastNameStart(result));
            ++i;
            continue;
        }
        result += name[i];
    }
    return result;
}

std::string_view Blueprint::namespaceName() const noexcept {
    // The last `::` outside template arguments.
    size_t depth = 0, pos = std::string::npos;
    for (size_t i = 0; i + 1 < name.size(); ++i) {
        if (name[i] == '<') ++depth;
        else if (name[i] == '>' && depth) --depth;
        else if (depth == 0 && name[i] == ':' && name[i + 1] == ':') pos = i;
    }
    return pos == std::string::npos
        ? std::string_view{}
        : std::string_view(name).substr(0, pos);
}

BlueprintId Blueprints::addComposition(std::string_view name, void (*compose)(::NodeBuild),
                                       void (*configure)(::NodeConfigure)) {
    if (!compose)
        throw Exception<Blueprints>("Composition callback is null");
    const auto id = add(name);
    require(id).meta.compose = compose;
    require(id).meta.configure = configure;
    return id;
}

BlueprintId Blueprints::addImplementation(const Implementation& implementation, std::string_view package) {
    if (!implementation.compose || implementation.api.empty() || implementation.name.empty())
        throw Exception<Blueprints>("Implementation needs an API, a name and a composition");
    const std::string name = package.empty()
        ? std::string(implementation.name)
        : std::format("{}::{}", package, implementation.name);
    BlueprintId api = find(implementation.api);
    if (api == InvalidId) api = add(implementation.api);
    const auto compose = implementation.compose;
    if (const BlueprintId existing = find(name); existing != InvalidId) {
        const auto& blueprint = require(existing);
        const auto* function = blueprint.meta.composeSlot.target<uint32_t (*)(::NodeBuild&)>();
        if (!function || *function != compose || blueprint.meta.configure != implementation.configure ||
            !isA(existing, api))
            throw Exception<Blueprints>("Implementation '{}' is already registered differently", name);
        return existing;
    }
    Blueprint blueprint{.name = name, .bases = {api}};
    blueprint.meta.composeSlot = compose;
    blueprint.meta.configure = implementation.configure;
    validateName(name);
    return Base::create(std::move(blueprint), name);
}

std::vector<BlueprintId> Blueprints::implementations(BlueprintId api) const {
    std::vector<BlueprintId> result;
    for (BlueprintId id = 0; id < size(); ++id)
        if (const auto* blueprint = get(id); blueprint && blueprint->meta.composeSlot && id != api && isA(id, api))
            result.push_back(id);
    return result;
}

BlueprintId Blueprints::add(std::string_view name, std::initializer_list<BlueprintId> bases) {
    validateName(name);

    std::vector<BlueprintId> uniqueBases;
    uniqueBases.reserve(bases.size());

    for (BlueprintId base : bases) {
        require(base);

        if (std::find(uniqueBases.begin(), uniqueBases.end(), base) == uniqueBases.end())
            uniqueBases.push_back(base);
    }

    Blueprint blueprint{
        .name = std::string(name),
        .bases = std::move(uniqueBases)
    };

    return Base::create(std::move(blueprint), std::string(name));
}

BlueprintId Blueprints::resolveImplementation(
    BlueprintId api,
    std::string_view nameSpace
) const {
    require(api);

    const auto compatible = [&](BlueprintId id) {
        const Blueprint* blueprint = get(id);

        return blueprint &&
               blueprint->meta.compose &&
               isA(id, api);
    };

    // An implementation from a package builds itself, like a composition.
    if (compatible(api) || require(api).meta.composeSlot)
        return api;

    const auto select = [&](bool local) {
        BlueprintId result = InvalidId;

        for (BlueprintId id = 0; id < size(); ++id) {
            const Blueprint* blueprint = get(id);

            if (!blueprint || !compatible(id))
                continue;

            if (local && blueprint->namespaceName() != nameSpace)
                continue;

            if (result != InvalidId)
                throw Exception<Blueprints>(
                    "Multiple implementations of '{}': '{}' and '{}'",
                    require(api).name,
                    require(result).name,
                    blueprint->name
                );

            result = id;
        }

        return result;
    };

    if (!nameSpace.empty()) {
        const BlueprintId local = select(true);

        if (local != InvalidId)
            return local;
    }

    const BlueprintId result = select(false);

    if (result == InvalidId)
        throw Exception<Blueprints>(
            "No constructible implementation of '{}'",
            require(api).name
        );

    return result;
}

void* Blueprints::cast(BlueprintId from, BlueprintId to, void* object) const {
    if (!object || !get(from) || !get(to))
        return nullptr;

    void* result = nullptr;

    auto walk = [&](auto&& self, BlueprintId id, void* ptr) -> void {
        if (id == to) {
            if (result && result != ptr)
                throw Exception<Blueprints>(
                    "Ambiguous conversion '{}' -> '{}'",
                    require(from).name,
                    require(to).name
                );

            result = ptr;
            return;
        }

        const Blueprint& blueprint = require(id);

        for (size_t i = 0; i < blueprint.upcasts.size(); ++i)
            if (blueprint.upcasts[i])
                self(self, blueprint.bases[i], blueprint.upcasts[i](ptr));
    };

    walk(walk, from, object);
    return result;
}

BlueprintId Blueprints::find(std::string_view name, std::string_view nameSpace) const {
    if (nameSpace.empty())
        return Base::find(std::string(name));

    return Base::find(std::string(nameSpace) + "::" + std::string(name));
}

BlueprintId Blueprints::resolve(std::string_view name) const {
    if (name.find("::") != std::string_view::npos)
        return find(name);

    BlueprintId result = InvalidId;

    for (BlueprintId id = 0; id < size(); ++id) {
        const Blueprint* blueprint = get(id);

        if (!blueprint || blueprint->shortName() != name)
            continue;

        if (result != InvalidId)
            throw Exception<Blueprints>(
                "Ambiguous name '{}': '{}' and '{}'",
                name,
                require(result).name,
                blueprint->name
            );

        result = id;
    }

    return result;
}

void Blueprints::addBase(BlueprintId derived, BlueprintId base) {
    require(derived);
    require(base);

    if (isA(base, derived))
        throw Exception<Blueprints>(
            "Inheritance cycle: '{}' -> '{}'",
            require(derived).name,
            require(base).name
        );

    auto& bases = Base::require(derived).bases;

    if (std::find(bases.begin(), bases.end(), base) == bases.end())
        bases.push_back(base);
}

bool Blueprints::isA(BlueprintId derived, BlueprintId base) const {
    if (!get(derived) || !get(base))
        return false;

    std::vector<bool> visited(size(), false);
    std::vector<BlueprintId> pending{derived};

    while (!pending.empty()) {
        const BlueprintId id = pending.back();
        pending.pop_back();

        if (id == base)
            return true;

        if (visited[id])
            continue;

        visited[id] = true;

        const auto& bases = require(id).bases;
        pending.insert(pending.end(), bases.begin(), bases.end());
    }

    return false;
}

void Blueprints::dumpTree() const {
    Lattice::TreeFormatter tree("Blueprints");

    auto append = [&](auto&& self, BlueprintId id, size_t depth) -> void {
        tree.node(
            std::format("{} <h>B</> <mut2>#{}</>", require(id).name, id),
            depth
        );

        for (BlueprintId child = 0; child < size(); ++child) {
            const Blueprint* blueprint = get(child);

            if (!blueprint)
                continue;

            if (std::find(blueprint->bases.begin(), blueprint->bases.end(), id) != blueprint->bases.end())
                self(self, child, depth + 1);
        }
    };

    for (BlueprintId id = 0; id < size(); ++id) {
        const Blueprint* blueprint = get(id);

        if (blueprint && blueprint->bases.empty())
            append(append, id, 0);
    }

    Logger::message(tree.format());
    Logger::blank();
}

void Blueprints::validateName(std::string_view name) {
    if (name.empty())
        throw Exception<Blueprints>("Blueprint name must not be empty");

    size_t begin = 0;

    while (begin < name.size()) {
        size_t end = name.find("::", begin);

        if (end == std::string_view::npos)
            end = name.size();

        const std::string_view part = name.substr(begin, end - begin);

        // clang names an anonymous namespace with a space: one part, like GCC's "{anonymous}".
        if (part.empty() || (part != "(anonymous namespace)" && part.find_first_of(": \t\r\n") != std::string_view::npos))
            throw Exception<Blueprints>("Invalid qualified name '{}'", name);

        if (end == name.size())
            return;

        begin = end + 2;
    }

    throw Exception<Blueprints>("Invalid qualified name '{}'", name);
}

} // namespace Lattice
