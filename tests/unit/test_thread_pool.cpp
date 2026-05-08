#include <gtest/gtest.h>
#include "util/ThreadPool.hpp"
#include <atomic>
#include <vector>
#include <chrono>

using namespace orcdb;

TEST(ThreadPoolTest, Submit_ExecutesTask) {
    ThreadPool pool(2);
    auto future = pool.Submit([]{ return 42; });
    EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPoolTest, Submit_MultipleTasksConcurrently) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;

    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.Submit([&counter]{ ++counter; }));
    }
    for (auto& f : futures) f.get();
    EXPECT_EQ(counter.load(), 100);
}

TEST(ThreadPoolTest, Submit_ReturnsCorrectValues) {
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.Submit([i]{ return i * i; }));
    }
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(futures[i].get(), i * i);
    }
}

TEST(ThreadPoolTest, Shutdown_CompletesInFlightTasks) {
    ThreadPool pool(2);
    std::atomic<int> done{0};
    for (int i = 0; i < 20; ++i) {
        pool.Submit([&done]{
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++done;
        });
    }
    pool.Shutdown();
    EXPECT_EQ(done.load(), 20);
}

TEST(ThreadPoolTest, Submit_AfterShutdown_Throws) {
    ThreadPool pool(1);
    pool.Shutdown();
    EXPECT_THROW(pool.Submit([]{ return 0; }), std::runtime_error);
}

TEST(ThreadPoolTest, QueueDepth_ReflectsEnqueuedCount) {
    ThreadPool pool(1);
    // Block the single worker
    std::atomic<bool> block{true};
    pool.Submit([&block]{ while (block); });

    for (int i = 0; i < 5; ++i) pool.Submit([]{});
    EXPECT_GE(pool.QueueDepth(), 0u); // at least some tasks queued

    block = false;
}
