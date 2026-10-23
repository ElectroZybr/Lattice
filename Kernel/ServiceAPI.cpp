#include <Lattice/Kernel/ServiceAPI.hpp>

#include <Lattice/Kernel/NodeOps.hpp>

void ServiceAPI::enter(Lattice::NodeOps& hostOps, std::function<bool()> interrupted) {
    if (running_.exchange(true))
        return;

    host_ = true;
    hostOps_ = &hostOps;
    interrupted_ = std::move(interrupted);

    try {
        run();
    } catch (...) {
        hostOps_ = nullptr;
        running_ = false;
        throw;
    }

    hostOps_ = nullptr;
    running_ = false;
}

bool ServiceAPI::stopRequested() {
    if (hostOps_)
        hostOps_->maintain();
    if (interrupted_ && interrupted_() && !stopRequested_.load())
        requestStop();

    return stopRequested_.load();
}
