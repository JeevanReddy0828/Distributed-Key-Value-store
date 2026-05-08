#include <gtest/gtest.h>
#include "storage/KVStore.hpp"
#include <thread>
#include <future>
#include <chrono>

using namespace orcdb;

TEST(WatchNotificationTest, WatchFires_AfterPut) {
    KVStore store;

    std::promise<std::string> p;
    auto future = p.get_future();

    uint64_t wid = store.Watch("config/feature-flag", false,
        [&p](const std::string&, const std::optional<KVEntry>& e) {
            if (e) {
                try { p.set_value(e->value); } catch(...) {}
            }
        });

    // Write from another thread with delay
    std::thread writer([&store]{
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        store.Put("config/feature-flag", "enabled", 1);
    });

    ASSERT_EQ(future.wait_for(std::chrono::milliseconds(1000)), std::future_status::ready);
    EXPECT_EQ(future.get(), "enabled");

    writer.join();
    store.Unwatch(wid);
}

TEST(WatchNotificationTest, WatchFires_AfterDelete) {
    KVStore store;
    store.Put("key", "val", 1);

    std::promise<bool> p;
    auto future = p.get_future();

    uint64_t wid = store.Watch("key", false,
        [&p](const std::string&, const std::optional<KVEntry>& e) {
            if (!e) {
                try { p.set_value(true); } catch(...) {}
            }
        });

    std::thread deleter([&store]{
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        store.Delete("key", 2);
    });

    ASSERT_EQ(future.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    EXPECT_TRUE(future.get());

    deleter.join();
    store.Unwatch(wid);
}

TEST(WatchNotificationTest, PrefixWatch_ReceivesAllMatchingUpdates) {
    KVStore store;
    std::atomic<int> count{0};

    uint64_t wid = store.Watch("/cfg/", true,
        [&count](const std::string&, const std::optional<KVEntry>&) { ++count; });

    store.Put("/cfg/db.host",  "localhost", 1);
    store.Put("/cfg/db.port",  "5432",      2);
    store.Put("/other/key",    "ignored",   3); // should NOT fire

    // Give callbacks time to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_EQ(count.load(), 2);
    store.Unwatch(wid);
}

TEST(WatchNotificationTest, Unwatch_StopsFiring) {
    KVStore store;
    std::atomic<int> count{0};

    uint64_t wid = store.Watch("k", false,
        [&count](const std::string&, const std::optional<KVEntry>&) { ++count; });

    store.Put("k", "v1", 1);
    store.Unwatch(wid);
    store.Put("k", "v2", 2); // should NOT fire

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(count.load(), 1);
}

TEST(WatchNotificationTest, MultipleWatchers_AllFire) {
    KVStore store;
    std::atomic<int> total{0};

    auto w1 = store.Watch("shared", false,
        [&total](const std::string&, const std::optional<KVEntry>&) { ++total; });
    auto w2 = store.Watch("shared", false,
        [&total](const std::string&, const std::optional<KVEntry>&) { ++total; });

    store.Put("shared", "value", 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(total.load(), 2);

    store.Unwatch(w1);
    store.Unwatch(w2);
}
