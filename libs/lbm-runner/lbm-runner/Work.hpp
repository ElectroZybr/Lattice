#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <lbm-runner/Axis.hpp>
#include <lbm-runner/Bench.hpp>
#include <lbm-runner/Visibility.hpp>
#include <Lattice/tools/Exception.hpp>
#include <Lattice/tools/TypeName.hpp>

/**
 @file Work.hpp
 @brief Работа и её реализации из любых единиц трансляции и модулей.

 Работа — контракт: вход (генератор по `N` и осям входа), проверка результата, эталон.
 Реализации регистрируются отдельно — в своих файлах и модулях; внутри набора они
 сводятся по `(группа, работа)`, между наборами одной машины — в `lbm`.

     // одна единица трансляции: работа
     BENCH_WORK(Walls, Constrain, TubeInput, "hard tube walls") {
         bench.config.sizes = {1'000, 1'000'000};       // как в группе
         work.input([](size_t n, const Run& run) { return TubeInput{n}; });
         work.check([](const TubeInput& expected, const TubeInput& actual) { return near(expected, actual); });
         work.reference("cpu");
     }

     // любая другая: реализация
     BENCH_IMPLEMENTATION(Walls, Constrain, "cpu", CpuTube) {
         impl.axis<Tasks::Workers>();
         impl.output([](CpuTube& p) { return p.state(); });
         impl.measure([](CpuTube& p, Run& run) { … run.value<Tasks::Workers>() … });
     }

 Фикстура реализации строится из входа: `Fixture(const Input&)` или
 `Fixture(const Input&, const Run&)`; вход генерируется один раз на `N` и значения осей
 входа и общий для всех реализаций. `measure` — последний вызов: он измеряет все варианты
 сразу, пока живы объекты тела (геометрия, устройство). Перед замером каждый вариант на
 наименьшем `N` сверяется с эталоном через `check`.
*/

namespace Lattice::Benchmarks {

class WorkBase {
public:
    WorkBase(std::string group, std::string name, std::string input)
        : group_(std::move(group)), name_(std::move(name)), input_(std::move(input)) {}
    virtual ~WorkBase() = default;

    const std::string& group() const noexcept { return group_; }
    const std::string& name() const noexcept { return name_; }
    /// Имя типа входа: реализация должна принимать тот же.
    const std::string& inputType() const noexcept { return input_; }
    const std::string& referenceName() const noexcept { return reference_; }
    const std::vector<AxisDecl>& axes() const noexcept { return axes_; }

    /// Ось входа: другие данные — каждая реализация проходит каждое значение.
    template<AxisType A>
    WorkBase& axis() {
        axes_.push_back(axisDecl<A>());
        return *this;
    }

    /// Реализация-эталон: остальные сверяются с ней через `check`.
    WorkBase& reference(std::string_view name) {
        reference_ = name;
        return *this;
    }

private:
    std::string group_, name_, input_;
    std::string reference_;
    std::vector<AxisDecl> axes_;
};

template<typename Input>
class Work final : public WorkBase {
public:
    using Generator = std::function<Input(size_t, const Run&)>;
    using Check = std::function<bool(const Input& expected, const Input& actual)>;

    Work(std::string group, std::string name) : WorkBase(std::move(group), std::move(name), std::string(typeName<Input>())) {}

    template<AxisType A>
    Work& axis() {
        WorkBase::axis<A>();
        return *this;
    }

    Work& reference(std::string_view name) {
        WorkBase::reference(name);
        return *this;
    }

    /// Детерминированный вход по `N` (и осям входа): `(n)` или `(n, const Run&)`.
    template<typename Function>
    Work& input(Function function) {
        if constexpr (std::is_invocable_v<Function&, size_t, const Run&>)
            generator_ = std::move(function);
        else
            generator_ = [function = std::move(function)](size_t n, const Run&) mutable { return function(n); };
        return *this;
    }

    /// Совпадение результата реализации с эталоном (допуск — явно в функции).
    Work& check(Check check) {
        check_ = std::move(check);
        return *this;
    }

    bool checks() const noexcept { return bool(check_); }
    bool same(const Input& expected, const Input& actual) const { return check_(expected, actual); }

    /// Вход на `N` при значениях осей входа: один раз, общий для всех реализаций.
    const Input& inputFor(size_t n, const Run& run) {
        if (!generator_)
            throw Exception<WorkBase>("work '{}/{}' has no input: declare work.input(...)", group(), name());
        const std::string key = std::format("{}{}", n, labels(run.values()));
        auto it = inputs_.find(key);
        if (it == inputs_.end())
            it = inputs_.emplace(key, std::make_unique<Input>(generator_(n, run))).first;
        return *it->second;
    }

    /// Результат эталона при значениях осей входа (ключ — их подписи).
    std::map<std::string, Input> expected;

private:
    Generator generator_;
    Check check_;
    std::map<std::string, std::unique_ptr<Input>> inputs_;
};

/// Реализация работы: оси исполнения, результат для сверки, замер.
template<typename FixtureType>
class Implementation {
public:
    using Input = typename FixtureType::Input;

    Implementation(Bench& bench, Work<Input>& work, std::string name)
        : bench_(bench), work_(work), name_(std::move(name)) {}

    const std::string& name() const noexcept { return name_; }

    /// Ось исполнения: тот же вход, каждое значение — вариант реализации (`cpu×16`).
    template<AxisType A>
    Implementation& axis() {
        axes_.push_back(axisDecl<A>());
        return *this;
    }

    /// Результат после шага — в типе входа работы, для сверки с эталоном.
    template<typename Function>
    Implementation& output(Function function) {
        output_ = std::move(function);
        return *this;
    }

    /// Сверить и измерить все варианты: вызывается последним, один раз.
    template<typename Function>
    void measure(Function function) {
        if (measured_)
            throw Exception<Implementation>("'{}/{}/{}': measure() is called twice", work_.group(), work_.name(), name_);
        measured_ = true;
        if (work_.checks() && !output_)
            throw Exception<Implementation>("'{}/{}/{}' has no output: the work checks results, declare impl.output(...)",
                work_.group(), work_.name(), name_);

        for (const auto& inputs : combinations(work_.axes())) {
            const Run inputRun(nullptr, inputs);
            for (const auto& params : combinations(axes_)) {
                Bench::Variant variant{.implementation = name_, .inputs = inputs, .params = params};
                if (work_.checks())
                    verify(variant, function);
                bench_.template measureVariant<FixtureType>(std::move(variant),
                    [this, inputRun](size_t n) -> std::unique_ptr<Fixture> {
                        Run run = inputRun;
                        run.setN(n);
                        return make(work_.inputFor(n, run), run);
                    },
                    function);
            }
        }
    }

    bool measured() const noexcept { return measured_; }

private:
    static std::unique_ptr<FixtureType> make(const Input& input, const Run& run) {
        if constexpr (std::is_constructible_v<FixtureType, const Input&, const Run&>)
            return std::make_unique<FixtureType>(input, run);
        else
            return std::make_unique<FixtureType>(input);
    }

    /// Один шаг на наименьшем N: первый вариант эталона задаёт ожидаемое, остальные
    /// сверяются с ним.
    template<typename Function>
    void verify(const Bench::Variant& variant, Function& function) {
        if (bench_.config.sizes.empty())
            return;
        size_t n = bench_.config.sizes.front();
        for (size_t size : bench_.config.sizes)
            n = std::min(n, size);

        std::vector<AxisValue> values = variant.inputs;
        values.insert(values.end(), variant.params.begin(), variant.params.end());
        Trace trace;
        Run run(&trace, std::move(values));
        run.setN(n);
        Run inputRun(nullptr, variant.inputs);
        inputRun.setN(n);
        auto fixture = make(work_.inputFor(n, inputRun), run);
        fixture->prepare();
        if constexpr (std::is_invocable_v<Function&, FixtureType&, Run&>)
            std::invoke(function, *fixture, run);
        else if constexpr (std::is_invocable_v<Function&, FixtureType&, Trace&>)
            std::invoke(function, *fixture, trace);
        else
            std::invoke(function, *fixture);
        Input actual = output_(*fixture);

        const std::string key = labels(variant.inputs);
        const std::string label = variant.implementation + labels(variant.params);
        const bool reference = variant.implementation == work_.referenceName();
        auto it = work_.expected.find(key);
        if (it == work_.expected.end()) {
            if (!reference)
                throw Exception<Implementation>("'{}/{}': the reference '{}' has no result to check '{}' against",
                    work_.group(), work_.name(), work_.referenceName(), label);
            work_.expected.emplace(key, std::move(actual));
            return;
        }
        if (!work_.same(it->second, actual))
            throw Exception<Implementation>("'{}/{}/{}{}' differs from the reference '{}' at N = {}",
                work_.group(), work_.name(), label, key, work_.referenceName(), n);
    }

    Bench& bench_;
    Work<Input>& work_;
    std::string name_;
    std::vector<AxisDecl> axes_;
    std::function<Input(FixtureType&)> output_;
    bool measured_ = false;
};

/// Регистрация (макросами ниже). Ошибки — дубль реализации, реализация без работы, работа
/// без реализаций, разный тип входа — при запуске работы, с местами регистрации.
struct WorkRegistration {
    std::string group, name, description, inputType;
    std::string where;   // file:line
    std::unique_ptr<WorkBase> (*create)(Bench&);
};

struct ImplementationRegistration {
    std::string group, work, name, inputType;
    std::string where;
    void (*run)(Bench&, WorkBase&, const ImplementationRegistration&);
};

BMRUNNER_API void registerWork(WorkRegistration registration);
BMRUNNER_API void registerImplementation(ImplementationRegistration registration);

namespace Detail {

template<typename Input, void (*Body)(Bench&, Work<Input>&)>
std::unique_ptr<WorkBase> createWork(Bench& bench) {
    auto work = std::make_unique<Work<Input>>(std::string(bench.group()), std::string(bench.name()));
    Body(bench, *work);
    return work;
}

template<typename FixtureType, void (*Body)(Implementation<FixtureType>&)>
void runImplementation(Bench& bench, WorkBase& work, const ImplementationRegistration& registration) {
    Implementation<FixtureType> impl(bench, static_cast<Work<typename FixtureType::Input>&>(work), registration.name);
    Body(impl);
    if (!impl.measured())
        throw Exception<WorkBase>("'{}/{}/{}' ({}) never calls impl.measure(...)", registration.group,
            registration.work, registration.name, registration.where);
}

struct WorkRegistrar {
    explicit WorkRegistrar(WorkRegistration registration) { registerWork(std::move(registration)); }
};

struct ImplementationRegistrar {
    explicit ImplementationRegistrar(ImplementationRegistration registration) {
        registerImplementation(std::move(registration));
    }
};

}

}

#define LATTICE_BENCH_CONCAT_(a, b) a##b
#define LATTICE_BENCH_CONCAT(a, b) LATTICE_BENCH_CONCAT_(a, b)
#define LATTICE_BENCH_STRING_(a) #a
#define LATTICE_BENCH_STRING(a) LATTICE_BENCH_STRING_(a)

#define BENCH_WORK3(group_, name_, Input_) BENCH_WORK4(group_, name_, Input_, "")
#define BENCH_WORK4(group_, name_, Input_, description_) \
    static void _bench_work_##group_##_##name_(::Lattice::Benchmarks::Bench& bench, \
        ::Lattice::Benchmarks::Work<Input_>& work); \
    static ::Lattice::Benchmarks::Detail::WorkRegistrar _bench_work_registrar_##group_##_##name_({ \
        .group = #group_, \
        .name = #name_, \
        .description = description_, \
        .inputType = std::string(::Lattice::typeName<Input_>()), \
        .where = __FILE__ ":" LATTICE_BENCH_STRING(__LINE__), \
        .create = &::Lattice::Benchmarks::Detail::createWork<Input_, &_bench_work_##group_##_##name_>, \
    }); \
    static void _bench_work_##group_##_##name_([[maybe_unused]] ::Lattice::Benchmarks::Bench& bench, \
        [[maybe_unused]] ::Lattice::Benchmarks::Work<Input_>& work)

#define BENCH_WORK_SELECT(_1, _2, _3, _4, NAME, ...) NAME
/// `BENCH_WORK(group, work, Input[, description]) { bench.config…; work.input(…); … }`
#define BENCH_WORK(...) BENCH_WORK_SELECT(__VA_ARGS__, BENCH_WORK4, BENCH_WORK3)(__VA_ARGS__)

#define BENCH_IMPLEMENTATION_(group_, work_, name_, Fixture_, id_) \
    static void LATTICE_BENCH_CONCAT(_bench_impl_, id_)(::Lattice::Benchmarks::Implementation<Fixture_>& impl); \
    static ::Lattice::Benchmarks::Detail::ImplementationRegistrar LATTICE_BENCH_CONCAT(_bench_impl_registrar_, id_)({ \
        .group = #group_, \
        .work = #work_, \
        .name = name_, \
        .inputType = std::string(::Lattice::typeName<typename Fixture_::Input>()), \
        .where = __FILE__ ":" LATTICE_BENCH_STRING(__LINE__), \
        .run = &::Lattice::Benchmarks::Detail::runImplementation<Fixture_, &LATTICE_BENCH_CONCAT(_bench_impl_, id_)>, \
    }); \
    static void LATTICE_BENCH_CONCAT(_bench_impl_, id_)([[maybe_unused]] ::Lattice::Benchmarks::Implementation<Fixture_>& impl)

/// `BENCH_IMPLEMENTATION(group, work, "name", Fixture) { impl.axis<A>(); impl.output(…); impl.measure(…); }`
/// — в любой единице трансляции набора; `Fixture::Input` — тип входа работы.
#define BENCH_IMPLEMENTATION(group_, work_, name_, Fixture_) \
    BENCH_IMPLEMENTATION_(group_, work_, name_, Fixture_, __COUNTER__)
