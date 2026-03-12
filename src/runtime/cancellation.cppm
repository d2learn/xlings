export module xlings.runtime.cancellation;

import std;

namespace xlings {

export struct CancelledException : std::runtime_error {
    CancelledException() : std::runtime_error("operation cancelled") {}
};

export struct PausedException : std::runtime_error {
    PausedException() : std::runtime_error("operation paused") {}
};

// State: 0=Active, 1=Paused, 2=Cancelled
// Uses atomic<int> instead of atomic<enum> to avoid GCC 15 module ICE.
export class CancellationToken {
    std::atomic<int> state_{0};
    std::mutex mtx_;
    std::condition_variable cv_;

public:
    void pause() {
        state_.store(1, std::memory_order_release);
        cv_.notify_all();
    }

    void resume() {
        state_.store(0, std::memory_order_release);
        cv_.notify_all();
    }

    void cancel() {
        state_.store(2, std::memory_order_release);
        cv_.notify_all();
    }

    void reset() {
        state_.store(0, std::memory_order_release);
    }

    bool is_active() const {
        return state_.load(std::memory_order_acquire) == 0;
    }

    bool is_paused() const {
        return state_.load(std::memory_order_acquire) == 1;
    }

    bool is_cancelled() const {
        return state_.load(std::memory_order_acquire) == 2;
    }

    void throw_if_cancelled() {
        auto s = state_.load(std::memory_order_acquire);
        if (s == 2) throw CancelledException{};
        if (s == 1) throw PausedException{};
    }

    // Wait on a condition variable while also checking for cancellation/pause and timeout.
    // Returns true if predicate was satisfied, false if cancelled/paused or timed out.
    template<typename Pred>
    bool wait_or_cancel(std::unique_lock<std::mutex>& lock,
                        std::condition_variable& cv, Pred pred,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) {
        auto deadline = (timeout.count() > 0)
            ? std::chrono::steady_clock::now() + timeout
            : std::chrono::steady_clock::time_point::max();

        while (!pred()) {
            if (!is_active()) return false;

            auto wait_until = std::min(
                deadline,
                std::chrono::steady_clock::now() + std::chrono::milliseconds{100});

            cv.wait_until(lock, wait_until);

            if (std::chrono::steady_clock::now() >= deadline) return false;
        }
        return true;
    }
};

} // namespace xlings
