#include <Lattice/kernel/StatusView.hpp>

#include <Lattice/kernel/NodeSystem.hpp>

namespace Lattice {

std::optional<Status::Snapshot> StatusView::status(NodeId id) const {
    const auto* status = nodes_ ? nodes_->ops.status(id) : nullptr;
    return status ? std::optional{status->read()} : std::nullopt;
}

uint64_t StatusView::observe(std::function<void(NodeId, const Status::Snapshot&)> callback) const {
    return nodes_ ? nodes_->ops.observeStatus(std::move(callback)) : 0;
}

void StatusView::unobserve(uint64_t token) const {
    if (nodes_) nodes_->ops.unobserveStatus(token);
}

}
