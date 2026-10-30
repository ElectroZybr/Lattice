// Parameter paths of Exports: `xmake run Lattice.bench Exports` or `lbm run Lattice Exports`.
// Set*: N writes of one float param — through Exports::set, the raw resolved setter, an
// executor on the caller's thread (dispatch cost alone) and an owner thread (a real queue);
// plain, with a range, refused by the range, through an own setter.
// Register: N params in Exports alone, the heap they take (Heap.bytes / N — bytes per param);
// RegisterNodes: N params as nodes declare them (8 common names a node, Context included).
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <lbm-runner/Benchmarks.hpp>
#include <lbm-runner/Metrics.hpp>
#include <lbm-runner/Metrics/Heap.hpp>
#include <Lattice/tests/RuntimeFixture.hpp>

using namespace Lattice;
using namespace Lattice::Benchmarks;

namespace {

struct Owner {
    float value = 0.0f;

    ParamResult setValue(const float& next) {
        value = next;
        return {};
    }
};

/// Runs work at once on the caller's thread: what dispatch costs without a thread switch.
struct InlineQueue {
    template<class F> void call(F&& work) { work(); }
    void post(std::function<void()> work) { work(); }
};

/// An owner thread serving commands, as a simulation between its steps.
class ThreadQueue {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> tasks_;
    bool stop_ = false;
    std::thread thread_;

    void serve() {
        std::unique_lock lock(mutex_);
        while (true) {
            wake_.wait(lock, [&] { return stop_ || !tasks_.empty(); });
            if (tasks_.empty()) return;
            auto tasks = std::move(tasks_);
            tasks_.clear();
            lock.unlock();
            for (auto& task : tasks) task();
            lock.lock();
        }
    }

public:
    ThreadQueue() : thread_([this] { serve(); }) {}
    ~ThreadQueue() {
        { std::lock_guard lock(mutex_); stop_ = true; }
        wake_.notify_all();
        thread_.join();
    }

    void post(std::function<void()> work) {
        { std::lock_guard lock(mutex_); tasks_.push_back(std::move(work)); }
        wake_.notify_one();
    }

    template<class F> void call(F&& work) {
        std::promise<void> done;
        auto future = done.get_future();
        post([&] { work(); done.set_value(); });
        future.get();
    }

    /// Wait until everything posted so far has run.
    void drain() { call([] {}); }
};

enum class Route { Direct, Inline, Thread };
enum class Kind { Plain, Ranged, Setter };

/// One float param of an object, its exports and (for Thread) the owner thread.
template<Route R, Kind K = Kind::Plain>
struct ParamFixture : Fixture {
    static constexpr bool Reuse = true;

    RuntimeFixture runtime;
    size_t n;
    ExportId id = InvalidExportId;
    ResolvedExport resolved;
    ThreadQueue* thread = nullptr;

    explicit ParamFixture(size_t n) : n(n) {
        auto root = runtime.build(runtime.root);
        auto owner = root.emplace<Owner>("owner");
        if constexpr (R == Route::Inline)
            owner.executeOn(root.emplace<InlineQueue>("queue"));
        if constexpr (R == Route::Thread) {
            auto queue = root.emplace<ThreadQueue>("queue");
            thread = queue.get();
            owner.executeOn(queue);
        }
        if constexpr (K == Kind::Plain) id = owner.param("value", &Owner::value);
        if constexpr (K == Kind::Ranged) id = owner.param("value", &Owner::value).range(0.0f, 1.0f);
        if constexpr (K == Kind::Setter) id = owner.param("value", &Owner::value, &Owner::setValue);
        resolved = exports().resolve(id);
    }

    Exports& exports() { return runtime.run_ctx.nodes.exports; }

    /// Everything written has reached the object.
    void settle() { if (thread) thread->drain(); }
};

template<Route R, Kind K = Kind::Plain>
void setAll(ParamFixture<R, K>& f) {
    for (size_t i = 0; i < f.n; ++i)
        (void)f.exports().set(f.id, Value{static_cast<double>(i % 7) * 0.01});
    f.settle();
}

using RangedFixture = ParamFixture<Route::Direct, Kind::Ranged>;
using SetterFixture = ParamFixture<Route::Direct, Kind::Setter>;
using RangedThreadFixture = ParamFixture<Route::Thread, Kind::Ranged>;
using SetterThreadFixture = ParamFixture<Route::Thread, Kind::Setter>;

/// Every write outside the range [0, 1]: the cost of a refusal.
void setRefused(ParamFixture<Route::Direct, Kind::Ranged>& f) {
    for (size_t i = 0; i < f.n; ++i)
        (void)f.exports().set(f.id, Value{2.0 + static_cast<double>(i % 7)});
}

template<Route R>
void setRaw(ParamFixture<R>& f) {
    for (size_t i = 0; i < f.n; ++i)
        (void)f.resolved.set(f.resolved.object, Value{static_cast<double>(i % 7) * 0.01});
    f.settle();
}

template<Route R>
double getAll(ParamFixture<R>& f) {
    double sum = 0;
    for (size_t i = 0; i < f.n; ++i)
        sum += f.exports().value(f.id).template get<double>();
    return sum;
}

/// A fresh node per iteration and the names of N params, ready before the measured part.
struct RegisterFixture : Fixture {
    size_t n;
    std::vector<std::string> names;
    std::optional<RuntimeFixture> runtime;
    std::unique_ptr<std::vector<Owner>> owners;

    explicit RegisterFixture(size_t n) : n(n) {
        for (size_t i = 0; i < n; ++i) names.push_back("p" + std::to_string(i));
    }

    void prepare() override {
        runtime.reset();
        runtime.emplace();
        owners = std::make_unique<std::vector<Owner>>(n);
    }
};

const std::vector<size_t> Sizes = {1'000, 10'000, 100'000};

}

BENCH_GROUP(Exports) {
    bench.config.sizes = Sizes;
}

BENCH_GROUPED(Exports, SetDirect, ParamFixture<Route::Direct>, "Exports::set, no executor") {
    bench.measure<ParamFixture<Route::Direct>>(setAll<Route::Direct>);
}

BENCH_GROUPED(Exports, SetRanged, RangedFixture, "Exports::set with a range, no executor") {
    bench.measure<ParamFixture<Route::Direct, Kind::Ranged>>(setAll<Route::Direct, Kind::Ranged>);
}

BENCH_GROUPED(Exports, SetRefused, RangedFixture, "Exports::set refused by the range") {
    bench.measure<ParamFixture<Route::Direct, Kind::Ranged>>(setRefused);
}

BENCH_GROUPED(Exports, SetSetter, SetterFixture, "Exports::set through an own setter, no executor") {
    bench.measure<ParamFixture<Route::Direct, Kind::Setter>>(setAll<Route::Direct, Kind::Setter>);
}

BENCH_GROUPED(Exports, SetRaw, ParamFixture<Route::Direct>, "resolved.set, no executor (input routing)") {
    bench.measure<ParamFixture<Route::Direct>>(setRaw<Route::Direct>);
}

BENCH_GROUPED(Exports, SetInline, ParamFixture<Route::Inline>, "Exports::set through an executor on the caller's thread") {
    bench.measure<ParamFixture<Route::Inline>>(setAll<Route::Inline>);
}

BENCH_GROUPED(Exports, SetThread, ParamFixture<Route::Thread>, "Exports::set through an owner thread") {
    bench.config.sizes = {1'000, 10'000};
    bench.measure<ParamFixture<Route::Thread>>(setAll<Route::Thread>);
}

BENCH_GROUPED(Exports, SetRangedThread, RangedThreadFixture, "Exports::set with a range through an owner thread") {
    bench.config.sizes = {1'000, 10'000};
    bench.measure<ParamFixture<Route::Thread, Kind::Ranged>>(setAll<Route::Thread, Kind::Ranged>);
}

BENCH_GROUPED(Exports, SetSetterThread, SetterThreadFixture, "Exports::set through an own setter on an owner thread (waits)") {
    bench.config.sizes = {1'000, 10'000};
    bench.measure<ParamFixture<Route::Thread, Kind::Setter>>(setAll<Route::Thread, Kind::Setter>);
}

BENCH_GROUPED(Exports, SetRawThread, ParamFixture<Route::Thread>, "resolved.set through an owner thread") {
    bench.config.sizes = {1'000, 10'000};
    bench.measure<ParamFixture<Route::Thread>>(setRaw<Route::Thread>);
}

BENCH_GROUPED(Exports, GetDirect, ParamFixture<Route::Direct>, "Exports::value, no executor") {
    bench.measure<ParamFixture<Route::Direct>>(getAll<Route::Direct>);
}

BENCH_GROUPED(Exports, Register, RegisterFixture, "N float params in Exports; Heap.bytes / N per param") {
    bench.stages.clear();
    bench.stages.add<Warmup>().samples(2);
    bench.stages.add<Time>().samples(5);
    bench.stages.add<Heap>().samples(5);
    bench.measure<RegisterFixture>([](RegisterFixture& f) {
        auto& exports = f.runtime->run_ctx.nodes.exports;
        auto& owners = *f.owners;
        for (size_t i = 0; i < f.n; ++i)
            exports.param(f.runtime->root, f.names[i], owners[i].value);
    });
}

BENCH_GROUPED(Exports, RegisterNodes, RegisterFixture, "N float params, 8 common names a node, Context included") {
    bench.stages.clear();
    bench.stages.add<Warmup>().samples(2);
    bench.stages.add<Time>().samples(5);
    bench.stages.add<Heap>().samples(5);
    bench.measure<RegisterFixture>([](RegisterFixture& f) {
        auto root = f.runtime->build(f.runtime->root);
        auto& owners = *f.owners;
        for (size_t i = 0; i < f.n; i += 8) {
            auto node = root.branch(f.names[i]);
            for (size_t k = 0; k < 8 && i + k < f.n; ++k)
                node.param(f.names[k], owners[i + k].value);
        }
    });
}
