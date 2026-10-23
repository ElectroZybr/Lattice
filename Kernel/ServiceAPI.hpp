#pragma once

#include <Lattice/Kernel/Consts.hpp>
#include <Lattice/Kernel/Visibility.hpp>

#include <atomic>
#include <functional>
#include <thread>
#include <utility>

namespace Lattice {
class NodeOps;
}

enum class ServiceLaunch {
    Worker,
    Host
};

// Kernel-internal service base; packages declare services with ObjectBuild::service.
struct LATTICE_API ServiceAPI {
    ServiceAPI() = default;
    ServiceAPI(const ServiceAPI&) = delete;
    ServiceAPI& operator=(const ServiceAPI&) = delete;
    ServiceAPI& operator=(ServiceAPI&&) = delete;

    void start() {
        if (running_.exchange(true))
            return;

        thread_ = std::thread([this] {
            run();
            running_ = false;
        });
    }

    /// Run on the calling thread. Each idle checkpoint services the runtime and asks
    /// the service to stop once `interrupted` reports an external stop (SIGINT).
    void enter(Lattice::NodeOps& hostOps, std::function<bool()> interrupted = {});

    void stop() {
        requestStop();
        if (thread_.joinable()) {
            thread_.join();
            running_ = false;
        }
    }

    bool running() const { return running_.load(); }
    bool host() const { return host_; }

    void retire() {
        requestStop();
    }

    bool readyToDestroy() const {
        return !running();
    }

    virtual ~ServiceAPI() {
        stop();
    }

protected:
    virtual void run() = 0;

    virtual void requestStop() {
        stopRequested_ = true;
    }

    bool stopRequested();

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    Lattice::NodeOps* hostOps_ = nullptr;
    std::function<bool()> interrupted_;
    bool host_ = false;
};


namespace Lattice {

/**
 * Execution state attached to an object's lifecycle by ObjectBuild::service.
 * It is not registered as a blueprint, runtime API or a separate tree node.
 *
 * The object does not inherit ServiceAPI. `run` receives an idle callback that the
 * object may call periodically; for a host service it services the runtime
 * (maintain), for a worker it is a no-op. `stop` asks `run` to return.
 */
class ServiceRunner final : public ServiceAPI {
public:
    using Idle = std::function<void()>;

    ServiceRunner(std::function<void(const Idle&)> run, std::function<void()> stop)
        : run_(std::move(run)), stop_(std::move(stop)) {}

    // ServiceAPI's destructor cannot reach the overridden requestStop.
    ~ServiceRunner() override { stop(); }

protected:
    void run() override {
        run_([this] { static_cast<void>(stopRequested()); });
    }

    void requestStop() override {
        ServiceAPI::requestStop();
        stop_();
    }

private:
    std::function<void(const Idle&)> run_;
    std::function<void()> stop_;
};

}
