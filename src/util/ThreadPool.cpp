#include "ThreadPool.hpp"

namespace orcdb {

ThreadPool::ThreadPool(size_t numThreads) {
    workers_.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back([this]{ WorkerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    Shutdown();
}

void ThreadPool::WorkerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this]{ return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            task = std::move(queue_.front());
            queue_.pop();
        }
        ++active_;
        task();
        --active_;
    }
}

void ThreadPool::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

size_t ThreadPool::QueueDepth() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
}

size_t ThreadPool::ActiveWorkers() const {
    return active_.load();
}

} // namespace orcdb
