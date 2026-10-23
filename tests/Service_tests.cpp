#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <Lattice/Composition.hpp>
#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/Tools/Tests.hpp>

namespace Lattice {
namespace {
// Ordinary class: no ServiceAPI, no Lattice headers needed by the implementation.
struct Worker {
    std::atomic<bool> stop{false};
    std::atomic<int> steps{0};
    std::atomic<int> idles{0};
    inline static int destroyed = 0;
    ~Worker() { ++destroyed; }

    void run() {
        while (!stop) {
            ++steps;
            std::this_thread::yield();
        }
    }
    void runWithIdle(const std::function<void()>& idle) {
        while (!stop) {
            ++steps;
            idle();
            ++idles;
            std::this_thread::yield();
        }
    }
    void requestStop() { stop = true; }
};

struct Consumer {
    explicit Consumer(Worker& worker) : worker(worker) {}
    Worker& worker;
};

ServiceAPI* onlyService(RuntimeFixture& fixture, NodeId branch) {
    auto& nodes = fixture.run_ctx.nodes;
    const auto api = fixture.run_ctx.blueprints.id<ServiceAPI>();
    const auto services = nodes.query.collect(branch, api);
    return services.size() == 1 ? static_cast<ServiceAPI*>(nodes.query.resolve(services.front(), api)) : nullptr;
}

template<class F>
bool eventually(F&& condition) {
    for (int i = 0; i < 2000 && !condition(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return condition();
}
}

TEST(Service_DeclaredInCompositionRunsOrdinaryObject, RuntimeFixture,
    "ObjectBuild::service запускает обычный объект как сервис ветки без наследования ServiceAPI.") {
    auto branch = fixture.build(fixture.root).branch("sim");
    auto worker = branch.emplace<Worker>("worker");
    worker.service(&Worker::run, &Worker::requestStop);

    auto* service = onlyService(fixture, branch.id());
    REQUIRE(service != nullptr);
    service->start();
    REQUIRE(eventually([&] { return worker->steps > 0; }));
    service->stop();
    REQUIRE(worker->stop);
    REQUIRE(!service->running());
}

TEST(Service_IdleCallbackIsPassedToRun, RuntimeFixture,
    "run(T&, idle) получает idle callback; в worker-режиме он ничего не делает.") {
    auto branch = fixture.build(fixture.root).branch("sim");
    auto worker = branch.emplace<Worker>("worker");
    worker.service(&Worker::runWithIdle, &Worker::requestStop);

    auto* service = onlyService(fixture, branch.id());
    REQUIRE(service != nullptr);
    service->start();
    REQUIRE(eventually([&] { return worker->idles > 0; }));
    service->stop();
    REQUIRE(!service->running());
}

TEST(Service_HostIdleMaintainsRuntime, RuntimeFixture,
    "В host-режиме idle обслуживает runtime: retirement завершается во время run.") {
    auto& nodes = fixture.run_ctx.nodes;
    auto root = fixture.build(fixture.root);
    auto doomed = root.branch("doomed");
    const NodeId doomedId = doomed.id();
    auto branch = root.branch("host");
    auto worker = branch.emplace<Worker>("worker");
    Worker* object = worker.get();
    bool retired = false;
    bool sameObject = false;
    worker.service([&, object](Worker& host, const std::function<void()>& idle) {
        sameObject = &host == object;
        nodes.ops.retireBranch(doomedId);
        while (!host.stop) {
            idle();
            if (!nodes.registry.get(doomedId)) {
                retired = true;
                host.requestStop();
            }
        }
    }, &Worker::requestStop);

    auto* service = onlyService(fixture, branch.id());
    REQUIRE(service != nullptr);
    service->enter(nodes.ops);
    REQUIRE(sameObject);
    REQUIRE(retired);
}

TEST(Service_RetirementStopsAndWaitsBeforeDestroy, RuntimeFixture,
    "Retirement ветки останавливает сервис и уничтожает объект после выхода из run.") {
    Worker::destroyed = 0;
    auto& nodes = fixture.run_ctx.nodes;
    auto branch = fixture.build(fixture.root).branch("sim");
    auto worker = branch.emplace<Worker>("worker");
    worker.service(&Worker::run, &Worker::requestStop);
    auto consumer = branch.emplace<Consumer>("consumer", worker);

    auto* service = onlyService(fixture, branch.id());
    service->start();
    REQUIRE(eventually([&] { return worker->steps > 0; }));

    nodes.ops.retireBranch(worker.id());
    REQUIRE(worker->stop);
    REQUIRE(nodes.registry.require(consumer.id()).state == NodeState::Retiring);
    REQUIRE(eventually([&] { nodes.ops.collectRetired(); return Worker::destroyed == 1; }));
    REQUIRE(!nodes.registry.get(consumer.id()));
}

TEST(Service_RollbackStopsServiceThatNeverStarted, RuntimeFixture,
    "Откат композиции уничтожает незапущенный сервис без ожидания.") {
    Worker::destroyed = 0;
    auto& nodes = fixture.run_ctx.nodes;
    const auto type = fixture.run_ctx.blueprints.addComposition("ServiceRollback", +[](NodeBuild root) {
        auto worker = root.emplace<Worker>("worker");
        worker.service(&Worker::run, &Worker::requestStop);
        throw std::runtime_error("composition failed");
    });
    bool failed = false;
    try { nodes.builder.add(fixture.root, type); }
    catch (const std::runtime_error&) { failed = true; }
    REQUIRE(failed);
    REQUIRE(Worker::destroyed == 1);
    REQUIRE(nodes.registry.children(fixture.root).empty());
}

}
