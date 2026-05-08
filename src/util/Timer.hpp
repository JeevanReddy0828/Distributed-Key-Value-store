#pragma once
#include <chrono>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace orcdb {

using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::steady_clock::time_point;
using Ms        = std::chrono::milliseconds;

inline TimePoint Now() { return Clock::now(); }

inline int64_t ElapsedMs(TimePoint from) {
    return std::chrono::duration_cast<Ms>(Now() - from).count();
}

// One-shot timer that fires a callback after a delay, cancellable.
class OneShotTimer {
public:
    OneShotTimer() = default;
    ~OneShotTimer() { Cancel(); }

    void Schedule(Ms delay, std::function<void()> cb) {
        Cancel();
        cancelled_ = false;
        thread_ = std::thread([this, delay, cb = std::move(cb)]() mutable {
            std::unique_lock<std::mutex> lock(mu_);
            if (cv_.wait_for(lock, delay, [this]{ return cancelled_.load(); })) {
                return; // cancelled before firing
            }
            if (!cancelled_) cb();
        });
    }

    void Cancel() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            cancelled_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    bool IsCancelled() const { return cancelled_; }

private:
    std::thread             thread_;
    std::mutex              mu_;
    std::condition_variable cv_;
    std::atomic<bool>       cancelled_{true};
};

// Measures elapsed wall-clock time.
class Stopwatch {
public:
    Stopwatch() : start_(Now()) {}
    void Reset() { start_ = Now(); }
    int64_t ElapsedMs() const { return orcdb::ElapsedMs(start_); }
    int64_t ElapsedUs() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(Now() - start_).count();
    }
private:
    TimePoint start_;
};

} // namespace orcdb
