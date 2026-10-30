#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <Lattice/kernel/Consts.hpp>
#include <Lattice/tools/Exception.hpp>
#include <Lattice/tools/TypeName.hpp>
#include <Lattice/kernel/Param.hpp>
#include <Lattice/kernel/Value.hpp>
#include <lattice-logger/Logger.hpp>
#include "Lattice/tools/TreeFormatter.hpp"

namespace Lattice {

enum class ContextResolutionState : uint8_t;

using ExportId = uint32_t;
inline constexpr ExportId InvalidExportId = std::numeric_limits<ExportId>::max();

enum class ExportKind : uint8_t {
    Param,
    Action
};

struct Param {
    void* object = nullptr;
    Value (*get)(const void*) = nullptr;
    ParamResult (*set)(void*, const Value&) = nullptr;
};

/**
 * Non-owning reference to a structured action result.
 *
 * The referenced object must remain alive until the caller has consumed the
 * ActionContext output.
 */
class ActionView {
public:
    template<typename T>
    explicit ActionView(T& object) noexcept
        : object_(&object), type_(typeKey<std::remove_cvref_t<T>>()) {}

    std::string_view type() const noexcept { return type_; }

    template<typename T>
    bool is() const noexcept {
        return type_ == typeKey<std::remove_cvref_t<T>>();
    }

    template<typename T>
    const std::remove_cvref_t<T>& as() const {
        using View = std::remove_cvref_t<T>;
        if (!is<View>())
            throw Exception<ActionView>(
                "View contains '{}', requested '{}'",
                type_,
                typeKey<View>()
            );
        return *static_cast<const View*>(object_);
    }

private:
    const void* object_ = nullptr;
    std::string_view type_;
};

using ActionOutput = std::variant<Value, ActionView>;

class ActionContext {
public:
    explicit ActionContext(NodeId node = InvalidNodeId) : node_(node) {}

    NodeId node() const noexcept { return node_; }
    void setNode(NodeId node) noexcept { node_ = node; }

    void emit(Value value) { output_.emplace_back(std::move(value)); }

    template<typename T>
    void present(const T& view) {
        output_.emplace_back(std::in_place_type<ActionView>, view);
    }

    template<typename T>
    void present(const T&&) = delete;

    std::span<const ActionOutput> output() const noexcept { return output_; }

private:
    NodeId node_ = InvalidNodeId;
    std::vector<ActionOutput> output_;
};

struct Action {
    std::function<void(ActionContext&, std::span<const Value>)> invoke;
    std::vector<Value> argumentTypes;
    std::vector<std::string> argumentNames;
    size_t requiredArguments = 0;
};

/// The names of an action's arguments, one per argument: tools show them, not the types.
template<size_t N>
struct ArgumentNames {
    std::array<std::string_view, N> names{};
    ArgumentNames() requires(N == 0) = default;
    template<class... S>
        requires(sizeof...(S) == N && N != 0 && (std::convertible_to<const S&, std::string_view> && ...))
    ArgumentNames(const S&... values) : names{std::string_view(values)...} {}
};

struct Export {
    std::string name;
    NodeId owner = InvalidNodeId;
    uint32_t slot = 0;  // index in the params or the actions of Exports, by kind
    ExportKind kind = ExportKind::Param;
    /// A param over functions of its source (accessor): the owner's executor is not its.
    bool accessor = false;
};

enum class ParamChange : uint8_t { Set, Reset, Remove };

struct ResolvedExport {
    ExportId id = InvalidExportId;
    void* object = nullptr;
    Value (*get)(const void*) = nullptr;
    ParamResult (*set)(void*, const Value&) = nullptr;
    void (*invoke)(void*, ActionContext&, std::span<const Value>) = nullptr;
    std::span<const Value> argumentTypes;
    std::span<const std::string> argumentNames;
    size_t requiredArguments = 0;

    explicit operator bool() const noexcept {
        return object || get || set || invoke;
    }
};

struct VisibleExport {
    RoleId role = InvalidRoleId;
    NodeId owner = InvalidNodeId;
    ExportId exportId = InvalidExportId;
    ContextScopeId scope = InvalidContextScopeId;
    ContextResolutionState state{};
    bool global = false;
};

/// Work on the thread that owns an object's state (its command queue).
struct Executor {
    /// Run work there and return once it is done.
    std::function<void(const std::function<void()>&)> call;
    /// Hand work over without waiting; empty — writes wait like reads.
    std::function<void(std::function<void()>)> post;
    /// Something was posted since the last settle().
    mutable std::atomic<bool> posted = false;
};

/// What a param has beyond a plain field: an owner thread, rules, its own setter.
struct ParamState {
    virtual ~ParamState() = default;
    virtual ParamRules rules() const = 0;
};

namespace detail {

template<class... Args>
inline constexpr bool leadingContext = false;
template<class... Rest>
inline constexpr bool leadingContext<ActionContext&, Rest...> = true;

/// Arguments of a method an action exports: a leading ActionContext& is not one.
template<class... Args>
inline constexpr size_t exportedArguments = sizeof...(Args) - (leadingContext<Args...> ? 1 : 0);

inline std::string_view valueKind(const Value& value) {
    static constexpr std::string_view names[] = {
        "string", "integer", "number", "bool", "vec2", "vec3", "vec4", "array", "object"};
    return names[value.index()];
}

template<typename T>
constexpr std::string_view paramTypeName() {
    if constexpr (std::is_same_v<T, std::string>) return "string";
    else if constexpr (std::is_same_v<T, bool>) return "bool";
    else if constexpr (std::is_floating_point_v<T>) return "number";
    else if constexpr (std::is_integral_v<T>) return "integer";
    else return typeName<T>();
}

template<typename T>
[[gnu::cold, gnu::noinline]] ParamResult typeMismatch(const Value& value) {
    return reject(ParamError::Type,
        std::format("expected {}, got {} {}", paramTypeName<T>(), valueKind(value), value.toString()));
}

template<typename T>
[[gnu::cold, gnu::noinline]] ParamResult typeMismatch(const Value& value, std::string_view reason) {
    return reject(ParamError::Type,
        std::format("expected {}, got {} {}: {}", paramTypeName<T>(), valueKind(value), value.toString(), reason));
}

/// Types Value holds itself: checked by the alternative, no exception on the way.
template<typename T>
constexpr bool BuiltinParam =
    std::is_arithmetic_v<T> || std::is_same_v<T, std::string> ||
    std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>;

/// The alternative of Value that holds a T, or nullptr: no throwing path on a hot write.
template<typename T>
const auto* held(const Value& value) noexcept {
    const auto& alternatives = static_cast<const Value::variant&>(value);
    if constexpr (std::is_same_v<T, bool>) return std::get_if<bool>(&alternatives);
    else if constexpr (std::is_floating_point_v<T>) return std::get_if<double>(&alternatives);
    else if constexpr (std::is_integral_v<T>) return std::get_if<int64_t>(&alternatives);
    else if constexpr (std::is_same_v<T, std::string>) return std::get_if<StringPtr>(&alternatives);
    else return std::get_if<T>(&alternatives);
}

template<typename T>
[[gnu::noinline]] ParamResult convertAdapted(T& target, const Value& value) {
    try {
        ParamAdapter<T>::set(target, value);
        return {};
    } catch (const std::exception& error) {
        return typeMismatch<T>(value, error.what());
    }
}

/// A builtin Value → T, or false when the value holds another type.
template<typename T>
bool assign(T& target, const Value& value) {
    const auto* alternative = held<T>(value);
    if (!alternative) [[unlikely]] return false;
    if constexpr (std::is_same_v<T, std::string>) target = **alternative;
    else target = static_cast<T>(*alternative);
    return true;
}

/// Value → T for a write; a value of another type is a rejection, not an exception.
template<typename T>
ParamResult convert(T& target, const Value& value) {
    if constexpr (BuiltinParam<T>) {
        if (!assign(target, value)) [[unlikely]] return typeMismatch<T>(value);
        return {};
    } else {
        return convertAdapted(target, value);
    }
}

template<typename T>
struct PlainParam {
    static Value get(const void* object) { return ParamAdapter<T>::get(*static_cast<const T*>(object)); }
    // No buffers here: the canary -fstack-protector-strong would add costs more than the write.
    [[gnu::no_stack_protector]] static ParamResult set(void* object, const Value& value) {
        return convert(*static_cast<T*>(object), value);
    }
};

}

/**
 * A param with rules, its own setter or an owner thread.
 *
 * A write converts and checks on the caller's thread; only applying it touches the owner's
 * state. A plain write to an owner thread is handed over without waiting (the queue keeps
 * its order before later reads and calls); one through an own setter waits for its answer.
 */
template<typename T>
struct TypedParam final : ParamState {
    static constexpr bool Ordered = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;
    struct NoBounds {};
    using Bounds = std::conditional_t<Ordered, std::optional<std::pair<T, T>>, NoBounds>;

    T* field = nullptr;
    const Executor* executor = nullptr;
    [[no_unique_address]] Bounds bounds{};
    std::vector<T> choices;
    size_t maxLength = std::numeric_limits<size_t>::max();
    std::function<ParamResult(const T&)> setter;

    bool accepts(const T& value) const {
        if constexpr (Ordered)
            if (bounds && !(value >= bounds->first && value <= bounds->second)) return false;
        if constexpr (std::equality_comparable<T>)
            if (!choices.empty() && std::ranges::find(choices, value) == choices.end()) return false;
        if constexpr (std::is_same_v<T, std::string>)
            if (value.size() > maxLength) return false;
        return true;
    }

    /// Why a value the rules do not accept is refused.
    [[gnu::cold, gnu::noinline]] ParamResult refuse(const T& value) const {
        if constexpr (Ordered)
            if (bounds && !(value >= bounds->first && value <= bounds->second)) return outOfRange(value);
        if constexpr (std::equality_comparable<T>)
            if (!choices.empty() && std::ranges::find(choices, value) == choices.end()) return notAChoice(value);
        if constexpr (std::is_same_v<T, std::string>)
            if (value.size() > maxLength) return tooLong(value);
        return {};
    }

    ParamResult outOfRange(const T& value) const {
        if constexpr (Ordered)
            return reject(ParamError::Range,
                std::format("{} is outside [{}..{}]", value, bounds->first, bounds->second));
        else
            return {};
    }

    ParamResult notAChoice(const T& value) const {
        std::vector<std::string> expected;
        for (const auto& choice : choices) expected.push_back(ParamAdapter<T>::get(choice).toString());
        return reject(ParamError::Choice,
            std::format("{} is not one of the choices", ParamAdapter<T>::get(value).toString()),
            0, 0, std::move(expected));
    }

    ParamResult tooLong(const T& value) const {
        if constexpr (std::is_same_v<T, std::string>)
            return reject(ParamError::Length,
                std::format("{} characters, at most {}", value.size(), maxLength), maxLength, value.size());
        else
            return {};
    }

    ParamResult apply(const T& value) {
        if (setter) return setter(value);
        *field = value;
        return {};
    }

    static double shown(T bound) {
        if constexpr (std::is_same_v<T, float>) return shortest(bound);
        else return static_cast<double>(bound);
    }

    ParamRules rules() const override {
        ParamRules result;
        if constexpr (Ordered)
            if (bounds) {
                result.min = shown(bounds->first);
                result.max = shown(bounds->second);
            }
        for (const auto& choice : choices) result.choices.push_back(ParamAdapter<T>::get(choice));
        if (maxLength != std::numeric_limits<size_t>::max()) result.maxLength = maxLength;
        result.setter = static_cast<bool>(setter);
        return result;
    }

    static Value get(const void* object) {
        const auto& self = *static_cast<const TypedParam*>(object);
        if (!self.executor) return ParamAdapter<T>::get(*self.field);
        Value result;
        self.executor->call([&] { result = ParamAdapter<T>::get(*self.field); });
        return result;
    }

    [[gnu::no_stack_protector]] static ParamResult set(void* object, const Value& value) {
        auto& self = *static_cast<TypedParam*>(object);
        if constexpr (std::is_default_constructible_v<T>) {
            T candidate{};
            if constexpr (detail::BuiltinParam<T>) {
                if (!detail::assign(candidate, value)) [[unlikely]] return detail::typeMismatch<T>(value);
            } else {
                if (auto converted = detail::convertAdapted(candidate, value); !converted) return converted;
            }
            if (!self.accepts(candidate)) [[unlikely]] return self.refuse(candidate);
            if (self.executor || self.setter) return self.handOver(std::move(candidate));
            *self.field = std::move(candidate);
            return {};
        } else {
            // Nothing to convert into off the owner's thread: the write runs there whole.
            ParamResult result;
            auto write = [&] { result = detail::convert(*self.field, value); };
            if (self.executor) self.executor->call(write);
            else write();
            return result;
        }
    }

    /// A value that passed the rules: to the own setter and the owner's thread.
    [[gnu::noinline]] ParamResult handOver(T candidate) {
        if (!executor) return setter(candidate);
        if (!setter && executor->post) {
            executor->posted.store(true, std::memory_order_relaxed);
            executor->post([field = field, candidate = std::move(candidate)] { *field = candidate; });
            return {};
        }
        ParamResult result;
        executor->call([&] { result = apply(candidate); });
        return result;
    }
};

/// A param without a field: read, write and the choices are functions of its source.
template<typename T>
struct AccessorParam final : ParamState {
    std::function<T()> read;
    std::function<ParamResult(const T&)> write;
    std::function<std::vector<T>()> options;

    ParamRules rules() const override {
        ParamRules result;
        if (options)
            for (const auto& option : options()) result.choices.push_back(ParamAdapter<T>::get(option));
        result.setter = true;
        return result;
    }

    static Value get(const void* object) {
        return ParamAdapter<T>::get(static_cast<const AccessorParam*>(object)->read());
    }

    static ParamResult set(void* object, const Value& value) {
        auto& self = *static_cast<AccessorParam*>(object);
        T candidate{};
        if (auto converted = detail::convert(candidate, value); !converted) return converted;
        return self.write(candidate);
    }
};

class Exports {
    std::vector<Export> exports_;
    std::vector<Param> params_;
    std::vector<Action> actions_;
    std::unordered_map<ExportId, std::unique_ptr<ParamState>> states_;
    std::unordered_map<NodeId, std::unique_ptr<Executor>> executors_;

    const Executor* executor(NodeId owner) const {
        const auto it = executors_.find(owner);
        return it == executors_.end() ? nullptr : it->second.get();
    }

    // Names: open addressing over exports and aliases, without a second copy of a name.
    static constexpr uint32_t EmptySlot = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t RemovedSlot = EmptySlot - 1;
    static constexpr uint32_t AliasEntry = 1u << 31;

    struct Alias {
        std::string name;
        NodeId owner = InvalidNodeId;
        ExportId target = InvalidExportId;
    };
    std::vector<Alias> aliases_;
    std::vector<uint32_t> index_;
    size_t indexUsed_ = 0;  // live and removed slots
    size_t indexLive_ = 0;

    static size_t hashKey(NodeId owner, std::string_view name) noexcept {
        return std::hash<std::string_view>{}(name) ^ (static_cast<size_t>(owner) * 0x9E3779B97F4A7C15ull);
    }

    std::pair<NodeId, std::string_view> keyOf(uint32_t entry) const {
        if (entry & AliasEntry) {
            const auto& alias = aliases_[entry & ~AliasEntry];
            return {alias.owner, alias.name};
        }
        return {exports_[entry].owner, exports_[entry].name};
    }

    uint32_t lookup(NodeId owner, std::string_view name) const {
        if (index_.empty()) return EmptySlot;
        const size_t mask = index_.size() - 1;
        for (size_t i = hashKey(owner, name) & mask;; i = (i + 1) & mask) {
            const uint32_t entry = index_[i];
            if (entry == EmptySlot) return EmptySlot;
            if (entry == RemovedSlot) continue;
            const auto [entryOwner, entryName] = keyOf(entry);
            if (entryOwner == owner && entryName == name) return entry;
        }
    }

    void place(uint32_t entry) {
        const auto [owner, name] = keyOf(entry);
        const size_t mask = index_.size() - 1;
        size_t i = hashKey(owner, name) & mask;
        while (index_[i] != EmptySlot) i = (i + 1) & mask;
        index_[i] = entry;
        ++indexUsed_;
        ++indexLive_;
    }

    void index(uint32_t entry) {
        if ((indexUsed_ + 1) * 4 > index_.size() * 3) {
            size_t size = 16;
            while (size * 3 < (indexLive_ + 1) * 8) size *= 2;  // live entries at most 3/8 after a rehash
            std::vector<uint32_t> old = std::exchange(index_, std::vector<uint32_t>(size, EmptySlot));
            indexUsed_ = indexLive_ = 0;
            for (uint32_t kept : old)
                if (kept != EmptySlot && kept != RemovedSlot) place(kept);
        }
        place(entry);
    }

    void claim(NodeId owner, std::string_view name) const {
        if (lookup(owner, name) != EmptySlot)
            throw Exception<Exports>("Export #{}:'{}' already exists", owner, name);
    }

    ExportId record(NodeId owner, std::string_view name, ExportKind kind, size_t slot) {
        const auto id = static_cast<ExportId>(exports_.size());
        exports_.push_back({
            .name = std::string(name),
            .owner = owner,
            .slot = static_cast<uint32_t>(slot),
            .kind = kind
        });
        index(id);
        return id;
    }

    [[noreturn, gnu::cold, gnu::noinline]] static void notWritable(ExportId id) {
        throw Exception<Exports>("Export #{} is not writable", id);
    }

    const Param& writable(ExportId id) const {
        if (id >= exports_.size() || exports_[id].owner == InvalidNodeId ||
            exports_[id].kind != ExportKind::Param || !params_[exports_[id].slot].set) [[unlikely]]
            notWritable(id);
        return params_[exports_[id].slot];
    }

    using Observer = std::function<void(ExportId, ParamChange)>;
    struct Observation {
        uint64_t token = 0;
        std::unique_ptr<Observer> callback;
    };
    std::vector<Observation> observers_;
    std::vector<std::unique_ptr<Observer>> unobserved_;  // dropped while notifying: freed after
    uint32_t notifying_ = 0;
    uint64_t nextObserver_ = 1;

    void notify(ExportId id, ParamChange change) {
        if (observers_.empty()) return;
        ++notifying_;
        struct Done {
            Exports& self;
            ~Done() {
                if (--self.notifying_ != 0 || self.unobserved_.empty()) return;
                self.unobserved_.clear();
                std::erase_if(self.observers_, [](const Observation& entry) { return !entry.callback; });
            }
        } done{*this};
        // Observers added meanwhile wait for the next change.
        const size_t count = observers_.size();
        for (size_t i = 0; i < count; ++i)
            if (const Observer* callback = observers_[i].callback.get())
                (*callback)(id, change);
    }

public:
    // Observe explicit parameter edits; direct C++ field writes and resolved raw
    // setters (e.g. transient input routing) do not emit these events.
    uint64_t observeParameters(Observer callback) {
        const auto token = nextObserver_++;
        observers_.push_back({token, std::make_unique<Observer>(std::move(callback))});
        return token;
    }

    void unobserveParameters(uint64_t token) {
        const auto it = std::ranges::find(observers_, token, &Observation::token);
        if (it == observers_.end()) return;
        if (notifying_ == 0) {
            observers_.erase(it);
            return;
        }
        unobserved_.push_back(std::move(it->callback));
    }

    void reset(ExportId id) {
        writable(id);
        notify(id, ParamChange::Reset);
    }

    /// Read, write and call every export of `owner` on the thread that owns the object's
    /// state: `call` runs work there and waits, `post` (optional) hands work over. A plain
    /// write is posted, so it costs no wait; reads, calls and own setters wait their turn
    /// after it, so observers and a later save see the new value. Declare it before the
    /// object's exports.
    void executeOn(
        NodeId owner,
        std::function<void(const std::function<void()>&)> call,
        std::function<void(std::function<void()>)> post = {}
    ) {
        for (const auto& entry : exports_)
            if (entry.owner == owner && !entry.accessor)
                throw Exception<Exports>("executeOn for node #{} must precede its exports ('{}')", owner, entry.name);
        auto run = std::make_unique<Executor>();
        run->call = std::move(call);
        run->post = std::move(post);
        executors_.insert_or_assign(owner, std::move(run));
    }

    /// Wait until the writes posted to the owner's thread have run: before the object goes.
    void settle(NodeId owner) {
        const auto it = executors_.find(owner);
        if (it == executors_.end() || !it->second->posted.exchange(false)) return;
        it->second->call([] {});
    }

    template<typename T>
    ExportId param(NodeId owner, std::string_view name, T& value) {
        claim(owner, name);
        const size_t slot = params_.size();
        std::unique_ptr<TypedParam<T>> state;
        if (const Executor* run = executor(owner)) {
            state = std::make_unique<TypedParam<T>>();
            state->field = &value;
            state->executor = run;
            params_.push_back({state.get(), &TypedParam<T>::get, &TypedParam<T>::set});
        } else {
            params_.push_back({&value, &detail::PlainParam<T>::get, &detail::PlainParam<T>::set});
        }
        const ExportId id = record(owner, name, ExportKind::Param, slot);
        if (state) states_.emplace(id, std::move(state));
        return id;
    }

    /// A param over functions of its source; it ignores executeOn: the source decides.
    template<typename T>
    ExportId accessor(
        NodeId owner,
        std::string_view name,
        std::function<T()> read,
        std::function<ParamResult(const T&)> write,
        std::function<std::vector<T>()> options = {}
    ) {
        claim(owner, name);
        auto state = std::make_unique<AccessorParam<T>>();
        state->read = std::move(read);
        state->write = std::move(write);
        state->options = std::move(options);
        const size_t slot = params_.size();
        params_.push_back({state.get(), &AccessorParam<T>::get, &AccessorParam<T>::set});
        const ExportId id = record(owner, name, ExportKind::Param, slot);
        exports_[id].accessor = true;
        states_.emplace(id, std::move(state));
        return id;
    }

    /// The typed state of a param, made on first use: for its rules and setter (composition).
    template<typename T>
    TypedParam<T>& typedParam(ExportId id) {
        writable(id);
        if (const auto it = states_.find(id); it != states_.end())
            return static_cast<TypedParam<T>&>(*it->second);
        Param& param = params_[exports_[id].slot];
        auto state = std::make_unique<TypedParam<T>>();
        state->field = static_cast<T*>(param.object);
        param = {state.get(), &TypedParam<T>::get, &TypedParam<T>::set};
        auto& typed = *state;
        states_.emplace(id, std::move(state));
        return typed;
    }

    /// Rules of a param for tools; empty when it has none.
    ParamRules rules(ExportId id) const {
        const auto it = states_.find(id);
        return it == states_.end() ? ParamRules{} : it->second->rules();
    }

    template<typename... Args, typename F>
    ExportId action(NodeId owner, std::string_view name, F&& callback,
                    ArgumentNames<sizeof...(Args)> names = {}) {
        static_assert(
            optionalArgumentsAreTrailing<Args...>(),
            "Optional action arguments must follow required arguments"
        );

        claim(owner, name);
        const size_t slot = actions_.size();

        using Callback = std::decay_t<F>;
        actions_.push_back({
            .invoke = [callback = Callback(std::forward<F>(callback))](
                ActionContext& context,
                std::span<const Value> arguments
            ) mutable {
                constexpr size_t required = requiredArgumentCount<Args...>();
                if (arguments.size() < required || arguments.size() > sizeof...(Args)) {
                    if constexpr (required == sizeof...(Args)) {
                        throw Exception<Action>(
                            "Expected {} arguments, received {}",
                            required,
                            arguments.size()
                        );
                    } else {
                        throw Exception<Action>(
                            "Expected {} to {} arguments, received {}",
                            required,
                            sizeof...(Args),
                            arguments.size()
                        );
                    }
                }

                invokeCallback<Args...>(callback, context, arguments, std::index_sequence_for<Args...>{});
            },
            .argumentTypes = {argumentType<Args>()...},
            .argumentNames = {names.names.begin(), names.names.end()},
            .requiredArguments = requiredArgumentCount<Args...>()
        });
        if (const Executor* run = executor(owner))
            actions_.back().invoke = [run, invoke = std::move(actions_.back().invoke)](
                ActionContext& context, std::span<const Value> arguments) mutable {
                run->call([&] { invoke(context, arguments); });
            };

        return record(owner, name, ExportKind::Action, slot);
    }

    /// Live exports owned by a node, in registration order (aliases excluded).
    std::vector<ExportId> owned(NodeId owner) const {
        std::vector<ExportId> result;
        for (ExportId id = 0; id < exports_.size(); ++id)
            if (exports_[id].owner == owner)
                result.push_back(id);
        return result;
    }

    ExportId find(NodeId owner, std::string_view name) const {
        const uint32_t entry = lookup(owner, name);
        if (entry == EmptySlot) return InvalidExportId;
        return entry & AliasEntry ? aliases_[entry & ~AliasEntry].target : entry;
    }

    void alias(NodeId owner, std::string_view name, ExportId target) {
        if (target >= exports_.size() || exports_[target].owner != owner)
            throw Exception<Exports>("Export #{} does not belong to node #{}", target, owner);
        claim(owner, name);
        aliases_.push_back({std::string(name), owner, target});
        index(static_cast<uint32_t>(aliases_.size() - 1) | AliasEntry);
    }

    /// Ids are given in order and never reused: an id at or above an earlier size is newer.
    ExportId size() const noexcept { return static_cast<ExportId>(exports_.size()); }

    const Export* get(ExportId id) const {
        return id < exports_.size() ? &exports_[id] : nullptr;
    }

    Value value(ExportId id) const {
        if (id >= exports_.size() || exports_[id].kind != ExportKind::Param || !params_[exports_[id].slot].get)
            throw Exception<Exports>("Export #{} is not readable", id);
        const Param& param = params_[exports_[id].slot];
        return param.get(param.object);
    }

    template<typename T>
    T value(ExportId id) const {
        return value(id).get<T>();
    }

    /// Write a param: type, rules, setter, then observers — only when it is accepted.
    [[gnu::no_stack_protector]] ParamResult set(ExportId id, const Value& value) {
        const Param& param = writable(id);
        ParamResult result = param.set(param.object, value);
        if (result) notify(id, ParamChange::Set);
        return result;
    }

    template<typename T>
    ParamResult set(ExportId id, T value) {
        return set(id, Value{std::move(value)});
    }

    void invoke(
        ExportId id,
        ActionContext& context,
        std::span<const Value> arguments = {}
    ) {
        if (id >= exports_.size() || exports_[id].kind != ExportKind::Action || !actions_[exports_[id].slot].invoke)
            throw Exception<Exports>("Export #{} is not callable", id);

        actions_[exports_[id].slot].invoke(context, arguments);
    }

    void invoke(ExportId id) {
        ActionContext context;
        invoke(id, context);
    }

    ResolvedExport resolve(ExportId id) {
        const auto& entry = exports_.at(id);

        if (entry.kind == ExportKind::Param) {
            const auto& param = params_[entry.slot];
            return {
                .id = id,
                .object = param.object,
                .get = param.get,
                .set = param.set
            };
        }

        auto& action = actions_[entry.slot];

        return {
            .id = id,
            .object = &action,
            .invoke = [](void* object, ActionContext& context, std::span<const Value> arguments) {
                static_cast<Action*>(object)->invoke(context, arguments);
            },
            .argumentTypes = action.argumentTypes,
            .argumentNames = action.argumentNames,
            .requiredArguments = action.requiredArguments
        };
    }

    /// Remove one export (a param the kernel made and no longer needs).
    void drop(ExportId id) {
        if (id >= exports_.size() || exports_[id].owner == InvalidNodeId) return;
        for (auto& entry : index_)
            if (entry == id) {
                entry = RemovedSlot;
                --indexLive_;
            }
        const bool parameter = exports_[id].kind == ExportKind::Param;
        exports_[id].owner = InvalidNodeId;
        if (parameter) params_[exports_[id].slot] = {};
        else actions_[exports_[id].slot] = {};
        states_.erase(id);
        if (parameter) notify(id, ParamChange::Remove);
    }

    void remove(NodeId owner) {
        settle(owner);
        if (!index_.empty())
            for (auto& entry : index_)
                if (entry != EmptySlot && entry != RemovedSlot && keyOf(entry).first == owner) {
                    entry = RemovedSlot;
                    --indexLive_;
                }
        // Exports go first (below); the executor is not used by anything after that.
        struct DropExecutor {
            std::unordered_map<NodeId, std::unique_ptr<Executor>>& executors;
            NodeId owner;
            ~DropExecutor() { executors.erase(owner); }
        } dropExecutor{executors_, owner};

        for (ExportId id = 0; id < exports_.size(); ++id) {
            if (exports_[id].owner != owner)
                continue;

            const bool parameter = exports_[id].kind == ExportKind::Param;
            exports_[id].owner = InvalidNodeId;
            if (parameter) params_[exports_[id].slot] = {};
            else actions_[exports_[id].slot] = {};
            states_.erase(id);
            if (parameter) notify(id, ParamChange::Remove);
        }
        for (auto& alias : aliases_)
            if (alias.owner == owner) alias = {};
    }

    void dump() const {
        Lattice::TreeFormatter tree("Exports");

        for (ExportId id = 0; id < exports_.size(); ++id) {
            const auto& entry = exports_[id];
            if (entry.owner == InvalidNodeId) continue;

            std::string value;

            if (entry.kind == ExportKind::Param) {
                const auto& param = params_[entry.slot];
                value = param.get ? param.get(param.object).toString() : "<unreadable>";
            }

            const std::string_view kind = entry.kind == ExportKind::Param ? "<ok>@</>" : "<wrn>λ</>";
            tree.node(std::format("{}{}<mut2> = {}</>", kind, entry.name, value), 0);
        }

        Logger::message(tree.format());
        Logger::blank();
    }

private:
    template<typename>
    static constexpr bool AlwaysFalse = false;

    template<typename T>
    struct OptionalArgument : std::false_type {
        using Type = std::remove_cvref_t<T>;
    };

    template<typename T>
    struct OptionalArgument<std::optional<T>> : std::true_type {
        using Type = T;
    };

    template<typename T>
    static constexpr bool IsOptionalArgument =
        OptionalArgument<std::remove_cvref_t<T>>::value;

    template<typename... Args>
    static consteval bool optionalArgumentsAreTrailing() {
        bool optionalSeen = false;
        bool valid = true;
        ((IsOptionalArgument<Args>
            ? optionalSeen = true
            : valid = valid && !optionalSeen), ...);
        return valid;
    }

    template<typename... Args>
    static consteval size_t requiredArgumentCount() {
        return (size_t{0} + ... + (IsOptionalArgument<Args> ? 0 : 1));
    }

    template<typename T>
    static Value argumentType() {
        using U = typename OptionalArgument<std::remove_cvref_t<T>>::Type;

        if constexpr (std::is_same_v<U, std::string>)
            return Value{std::string{}};
        else if constexpr (std::is_same_v<U, bool>)
            return Value{false};
        else if constexpr (std::is_integral_v<U>)
            return Value{int64_t{0}};
        else if constexpr (std::is_floating_point_v<U>)
            return Value{0.0};
        else if constexpr (
            std::is_same_v<U, glm::vec2> ||
            std::is_same_v<U, glm::vec3> ||
            std::is_same_v<U, glm::vec4>
        )
            return Value{U{}};
        else
            static_assert(AlwaysFalse<U>, "Unsupported action argument type");
    }

    template<typename T>
    static std::remove_cvref_t<T> argument(
        std::span<const Value> arguments,
        size_t index
    ) {
        using U = std::remove_cvref_t<T>;

        if constexpr (IsOptionalArgument<U>) {
            using ValueType = typename OptionalArgument<U>::Type;
            if (index >= arguments.size())
                return std::nullopt;
            return U{arguments[index].as<ValueType>()};
        } else {
            return arguments[index].as<U>();
        }
    }

    template<typename... Args, typename F, size_t... Indices>
    static void invokeCallback(
        F& callback,
        ActionContext& context,
        std::span<const Value> arguments,
        std::index_sequence<Indices...>
    ) {
        if constexpr (std::is_invocable_v<F&, ActionContext&, std::remove_cvref_t<Args>...>)
            std::invoke(callback, context, argument<Args>(arguments, Indices)...);
        else if constexpr (std::is_invocable_v<F&, std::remove_cvref_t<Args>...>)
            std::invoke(callback, argument<Args>(arguments, Indices)...);
        else
            static_assert(AlwaysFalse<F>, "Action callback does not match its declared arguments");
    }
};


/// A param being declared: its ExportId, plus rules and an own setter (composition only).
template<typename T>
class ParamBuild {
    Exports* exports_ = nullptr;
    ExportId id_ = InvalidExportId;

    TypedParam<T>& typed() {
        static_assert(std::is_default_constructible_v<T>,
            "Rules and setters convert a value before it is applied: the param type must be default-constructible");
        return exports_->template typedParam<T>(id_);
    }

public:
    ParamBuild(Exports& exports, ExportId id) : exports_(&exports), id_(id) {}

    operator ExportId() const noexcept { return id_; }
    ExportId id() const noexcept { return id_; }

    /// Accept only [min, max], both ends included; NaN never.
    ParamBuild& range(T min, T max) requires TypedParam<T>::Ordered {
        if (!(min <= max))
            throw Exception<ParamBuild>("Empty range [{}, {}]", min, max);
        typed().bounds = std::pair{min, max};
        return *this;
    }

    ParamBuild& choices(std::vector<T> values) requires std::equality_comparable<T> {
        if (values.empty())
            throw Exception<ParamBuild>("No choices for export #{}", id_);
        typed().choices = std::move(values);
        return *this;
    }

    ParamBuild& maxLength(size_t length) requires std::same_as<T, std::string> {
        typed().maxLength = length;
        return *this;
    }

    /// Apply a value that passed the rules, or refuse it; runs on the owner's thread.
    ParamBuild& setter(std::function<ParamResult(const T&)> apply) {
        typed().setter = std::move(apply);
        return *this;
    }
};

}
