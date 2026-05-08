#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <stdexcept>

namespace orcdb {

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    template<typename F, typename... Args>
    auto Submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<ReturnType> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (stop_) throw std::runtime_error("ThreadPool is stopped");
            queue_.emplace([task]{ (*task)(); });
        }
        cv_.notify_one();
        return result;
    }

    void Shutdown();
    size_t QueueDepth() const;
    size_t ActiveWorkers() const;
    size_t NumThreads() const { return workers_.size(); }

private:
    void WorkerLoop();

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> queue_;
    mutable std::mutex                mu_;
    std::condition_variable           cv_;
    std::atomic<bool>                 stop_{false};
    std::atomic<size_t>               active_{0};
};

} // namespace orcdb
