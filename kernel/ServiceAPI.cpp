#include <Lattice/kernel/ServiceAPI.hpp>

#include <Lattice/kernel/NodeOps.hpp>
#include <lattice-logger/Logger.hpp>

void ServiceAPI::enter(Lattice::NodeOps& hostOps, std::function<bool()> interrupted) {
    if (running_.exchange(true))
        return;

    host_ = true;
    hostOps_ = &hostOps;
    interrupted_ = std::move(interrupted);
    status_.set(Lattice::Status::State::Ready);

    try {
        run();
    } catch (const std::exception& error) {
        status_.set(Lattice::Status::State::Failed, error.what());
        hostOps_ = nullptr;
        running_ = false;
        throw;
    } catch (...) {
        status_.set(Lattice::Status::State::Failed, "unknown exception");
        hostOps_ = nullptr;
        running_ = false;
        throw;
    }

    status_.set(Lattice::Status::State::Left);
    hostOps_ = nullptr;
    running_ = false;
}

void ServiceAPI::runReporting() {
    try {
        run();
        status_.set(Lattice::Status::State::Left);
    } catch (const std::exception& error) {
        status_.set(Lattice::Status::State::Failed, error.what());
        Logger::error("Service", "stopped by an exception: {}", error.what());
    } catch (...) {
        status_.set(Lattice::Status::State::Failed, "unknown exception");
        Logger::error("Service", "stopped by an unknown exception");
    }
}

bool ServiceAPI::stopRequested() {
    beats_ = true;
    status_.touch();
    if (hostOps_)
        hostOps_->maintain();
    if (interrupted_ && interrupted_() && !stopRequested_.load())
        requestStop();

    return stopRequested_.load();
}
