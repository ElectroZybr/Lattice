#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace Lattice {

/**
 * Liveness of work that goes on by itself: a service, a device, a remote node. An ordinary
 * object holds one and writes it from any thread; composition shows it on the node
 * (ObjectBuild::status), and tools read it without the owner's queue — a stuck owner still
 * shows itself. Header-only: plain classes use it without linking the runtime.
 */
class Status {
public:
    enum class State : uint8_t {
        Ready,      // working, fresh
        Connecting, // not working yet, or working worse than it should
        Stalled,    // no sign of life for a while
        Lost,       // gone without saying
        Failed,     // stopped by an error
        Left,       // stopped on purpose
        Offline,    // not there now: never reached, or waiting for it to come
    };

    struct Snapshot {
        State state = State::Ready;
        std::string detail;
        std::chrono::steady_clock::time_point updated{};
        /// A delay the work measures itself: a round trip, a queue.
        std::optional<std::chrono::nanoseconds> latency;
        /// Grows with every set and touch: a reader sees whether anything changed.
        uint64_t version = 0;
        /// Grows when the state or its detail changes: what observers react to.
        uint64_t changes = 0;
        /// It has given a sign of life (touch): its silence means something.
        bool beats = false;
    };

    static std::string_view name(State state) {
        constexpr std::string_view names[] = {"up", "connecting", "stalled", "lost", "failed", "left", "offline"};
        return names[static_cast<size_t>(state)];
    }

    void set(State state, std::string detail = {}) {
        std::lock_guard lock(mutex_);
        if (snapshot_.state != state || snapshot_.detail != detail) ++snapshot_.changes;
        snapshot_.state = state;
        snapshot_.detail = std::move(detail);
        snapshot_.updated = std::chrono::steady_clock::now();
        ++snapshot_.version;
    }

    /// A sign of life: fresh again, with a new latency when it is known. Stalled becomes ready.
    void touch(std::optional<std::chrono::nanoseconds> latency = std::nullopt) {
        std::lock_guard lock(mutex_);
        if (snapshot_.state == State::Stalled) {
            snapshot_.state = State::Ready;
            snapshot_.detail.clear();
            ++snapshot_.changes;
        }
        if (latency) snapshot_.latency = latency;
        snapshot_.beats = true;
        snapshot_.updated = std::chrono::steady_clock::now();
        ++snapshot_.version;
    }

    Snapshot read() const {
        std::lock_guard lock(mutex_);
        return snapshot_;
    }

private:
    mutable std::mutex mutex_;
    Snapshot snapshot_{.updated = std::chrono::steady_clock::now()};
};

}
