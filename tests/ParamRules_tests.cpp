#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/tools/Tests.hpp>

#include <Lattice/Lattice.hpp>

namespace Lattice::ParamRulesTests {

struct Settings {
    float dt = 0.01f;
    int64_t steps = 10;
    std::string mode = "shaded";
    std::string label;
    std::string shape = "sphere(1)";
    std::thread::id applied;

    ParamResult setShape(const std::string& text) {
        applied = std::this_thread::get_id();
        if (const auto at = text.find('?'); at != std::string::npos)
            return reject(ParamError::Invalid, "unexpected '?'", at, at + 1, {"sphere", "box"});
        shape = text;
        return {};
    }
};

/// Commands of an owner: posted work waits for the owner's next turn (any call).
struct DeferringQueue {
    std::vector<std::function<void()>> pending;
    int calls = 0;
    int posts = 0;

    void post(std::function<void()> work) {
        ++posts;
        pending.push_back(std::move(work));
    }

    template<class F>
    void call(F&& work) {
        ++calls;
        runPending();
        work();
    }

    void runPending() {
        auto ready = std::move(pending);
        pending.clear();
        for (auto& work : ready) work();
    }
};

/// Runs every call on a thread of its own.
struct ThreadQueue {
    template<class F>
    void call(F&& work) { std::thread([&] { work(); }).join(); }
};

/// Records what its field held when it was destroyed.
struct Tracked {
    float value = 0.0f;
    float* last = nullptr;
    explicit Tracked(float* last) : last(last) {}
    ~Tracked() { *last = value; }
};

struct Counted {
    int changes = 0;
    uint64_t token = 0;
    Exports* exports = nullptr;

    explicit Counted(Exports& exports) : exports(&exports) {
        token = exports.observeParameters([this](ExportId, ParamChange change) {
            if (change == ParamChange::Set) ++changes;
        });
    }
    ~Counted() { exports->unobserveParameters(token); }
};

TEST(ParamRules_RangeIncludesEnds, RuntimeFixture,
    "range: концы входят, вне диапазона и NaN — отказ Range без записи и уведомления.") {
    auto& exports = fixture.run_ctx.nodes.exports;
    auto settings = fixture.build(fixture.root).emplace<Settings>("settings");
    const ExportId dt = settings.param("dt", &Settings::dt).range(1e-6f, 0.1f);
    Counted observed{exports};

    REQUIRE(exports.set(dt, 0.1));
    REQUIRE(exports.set(dt, 1e-6));
    REQUIRE(observed.changes == 2);

    for (double bad : {0.5, -1.0, std::nan(""), std::numeric_limits<double>::infinity()}) {
        const auto result = exports.set(dt, bad);
        REQUIRE(!result);
        REQUIRE(result.rejection().code == ParamError::Range);
    }
    REQUIRE(settings->dt == 1e-6f);
    REQUIRE(observed.changes == 2);

    const auto rules = exports.rules(dt);
    REQUIRE(rules.min && rules.max && *rules.min == 1e-6 && *rules.max == 0.1);
    REQUIRE(!rules.setter && rules.choices.empty());
}

TEST(ParamRules_ChoicesAndLength, RuntimeFixture,
    "choices: отказ Choice со списком допустимого; maxLength: отказ Length с участком.") {
    auto& exports = fixture.run_ctx.nodes.exports;
    auto settings = fixture.build(fixture.root).emplace<Settings>("settings");
    const ExportId mode = settings.param("mode", &Settings::mode).choices({"shaded", "wireframe"});
    const ExportId label = settings.param("label", &Settings::label).maxLength(4);

    REQUIRE(exports.set(mode, std::string{"wireframe"}));
    const auto refused = exports.set(mode, std::string{"Shaded"});
    REQUIRE(!refused && refused.rejection().code == ParamError::Choice);
    REQUIRE(refused.rejection().expected.size() == 2);
    REQUIRE(settings->mode == "wireframe");
    REQUIRE(exports.rules(mode).choices.size() == 2);

    REQUIRE(exports.set(label, std::string{"abcd"}));
    const auto tooLong = exports.set(label, std::string{"abcdef"});
    REQUIRE(!tooLong && tooLong.rejection().code == ParamError::Length);
    REQUIRE(tooLong.rejection().begin == 4 && tooLong.rejection().end == 6);
    REQUIRE(settings->label == "abcd");
    REQUIRE(exports.rules(label).maxLength == 4u);
}

TEST(ParamRules_TypeMismatchIsARejection, RuntimeFixture,
    "Значение другого типа — отказ Type, а не исключение; поле не меняется.") {
    auto& exports = fixture.run_ctx.nodes.exports;
    auto settings = fixture.build(fixture.root).emplace<Settings>("settings");
    const ExportId plain = settings.param("steps", &Settings::steps);
    const ExportId ranged = settings.param("dt", &Settings::dt).range(0.0f, 1.0f);

    for (ExportId id : {plain, ranged}) {
        const auto result = exports.set(id, std::string{"fast"});
        REQUIRE(!result && result.rejection().code == ParamError::Type);
    }
    REQUIRE(settings->steps == 10 && settings->dt == 0.01f);
}

TEST(ParamRules_RawSetterChecksToo, RuntimeFixture,
    "Сырой resolved.set проверяет правила так же: обойти их нечем.") {
    auto& exports = fixture.run_ctx.nodes.exports;
    auto settings = fixture.build(fixture.root).emplace<Settings>("settings");
    const ExportId dt = settings.param("dt", &Settings::dt).range(0.0f, 1.0f);
    const auto resolved = exports.resolve(dt);

    REQUIRE(!resolved.set(resolved.object, Value{2.0}));
    REQUIRE(resolved.set(resolved.object, Value{0.5}));
    REQUIRE(settings->dt == 0.5f);
}

TEST(ParamRules_OwnSetter, RuntimeFixture,
    "Свой сеттер — метод или лямбда: применяет или отказывает с участком; правила — до него.") {
    auto& exports = fixture.run_ctx.nodes.exports;
    auto settings = fixture.build(fixture.root).emplace<Settings>("settings");
    const ExportId shape = settings.param("shape", &Settings::shape, &Settings::setShape).maxLength(16);
    int lambdaCalls = 0;
    const ExportId steps = settings.param("steps", &Settings::steps,
        [&](Settings& target, const int64_t& value) -> ParamResult {
            ++lambdaCalls;
            if (value % 2) return reject(ParamError::Invalid, "steps must be even");
            target.steps = value;
            return {};
        });
    Counted observed{exports};

    REQUIRE(exports.set(shape, std::string{"box(2)"}));
    REQUIRE(settings->shape == "box(2)");

    const auto refused = exports.set(shape, std::string{"box(?)"});
    REQUIRE(!refused && refused.rejection().code == ParamError::Invalid);
    REQUIRE(refused.rejection().begin == 4 && refused.rejection().end == 5);
    REQUIRE(refused.rejection().expected.size() == 2);
    REQUIRE(settings->shape == "box(2)");

    const auto tooLong = exports.set(shape, std::string(20, 'x'));
    REQUIRE(!tooLong && tooLong.rejection().code == ParamError::Length);
    REQUIRE(settings->shape == "box(2)");

    REQUIRE(!exports.set(steps, int64_t(3)));
    REQUIRE(exports.set(steps, int64_t(4)));
    REQUIRE(settings->steps == 4 && lambdaCalls == 2);
    REQUIRE(observed.changes == 2);
    REQUIRE(exports.rules(shape).setter && exports.rules(shape).maxLength == 16u);
}

TEST(ParamRules_OwnerThread, RuntimeFixture,
    "executeOn: простая запись уходит без ожидания и видна следующему чтению; сеттер — в "
    "потоке владельца с ответом; отказ по правилам не заходит в очередь.") {
    auto& exports = fixture.run_ctx.nodes.exports;
    auto root = fixture.build(fixture.root);
    auto queue = root.emplace<DeferringQueue>("queue");
    auto settings = root.emplace<Settings>("settings");
    settings.executeOn(queue);
    const ExportId dt = settings.param("dt", &Settings::dt).range(0.0f, 1.0f);
    const ExportId shape = settings.param("shape", &Settings::shape, &Settings::setShape);

    REQUIRE(exports.set(dt, 0.5));
    REQUIRE(queue->posts == 1 && queue->calls == 0);
    REQUIRE(settings->dt == 0.01f);
    REQUIRE(exports.value<double>(dt) == 0.5);
    REQUIRE(queue->calls == 1 && queue->pending.empty());

    REQUIRE(!exports.set(dt, 2.0));
    REQUIRE(queue->posts == 1 && queue->calls == 1);

    REQUIRE(!exports.set(shape, std::string{"?"}));
    REQUIRE(queue->calls == 2);
    REQUIRE(exports.set(shape, std::string{"box(1)"}));
    REQUIRE(queue->calls == 3 && settings->shape == "box(1)");
}

TEST(ParamRules_SetterRunsOnOwnerThread, RuntimeFixture,
    "Свой сеттер выполняется в потоке, который обслуживает очередь владельца.") {
    auto& exports = fixture.run_ctx.nodes.exports;
    auto root = fixture.build(fixture.root);
    auto queue = root.emplace<ThreadQueue>("queue");
    auto settings = root.emplace<Settings>("settings");
    settings.executeOn(queue);
    const ExportId shape = settings.param("shape", &Settings::shape, &Settings::setShape);

    REQUIRE(exports.set(shape, std::string{"box(1)"}));
    REQUIRE(settings->applied != std::thread::id{});
    REQUIRE(settings->applied != std::this_thread::get_id());
}

TEST(ParamRules_PostedWriteLandsBeforeDestruction, RuntimeFixture,
    "Отложенная запись доходит до объекта до его уничтожения.") {
    auto& exports = fixture.run_ctx.nodes.exports;
    auto root = fixture.build(fixture.root);
    auto queue = root.emplace<DeferringQueue>("queue");
    float last = -1.0f;
    auto branch = root.branch("branch");
    auto tracked = branch.emplace<Tracked>("tracked", &last);
    tracked.executeOn(queue);
    const ExportId value = tracked.param("value", &Tracked::value);

    REQUIRE(exports.set(value, 0.75));
    REQUIRE(queue->pending.size() == 1);
    fixture.run_ctx.nodes.ops.destroyBranch(branch.id());
    REQUIRE(last == 0.75f);
    REQUIRE(queue->pending.empty());
}

TEST(ParamRules_EmptyRangeIsRefused, RuntimeFixture,
    "Пустой диапазон и пустой список выбора — ошибка объявления.") {
    auto settings = fixture.build(fixture.root).emplace<Settings>("settings");
    bool emptyRange = false;
    try { settings.param("dt", &Settings::dt).range(1.0f, 0.0f); } catch (const ExceptionBase&) { emptyRange = true; }
    REQUIRE(emptyRange);
    bool noChoices = false;
    try { settings.param("mode", &Settings::mode).choices({}); } catch (const ExceptionBase&) { noChoices = true; }
    REQUIRE(noChoices);
}

}
