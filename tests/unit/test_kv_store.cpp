#include <gtest/gtest.h>
#include "storage/KVStore.hpp"
#include <thread>
#include <vector>
#include <atomic>

using namespace orcdb;

class KVStoreTest : public ::testing::Test {
protected:
    KVStore store;
};

TEST_F(KVStoreTest, PutAndGet_BasicRoundTrip) {
    ASSERT_TRUE(store.Put("foo", "bar", 1));
    auto entry = store.Get("foo");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->value, "bar");
    EXPECT_EQ(entry->version, 1);
    EXPECT_EQ(entry->raftIndex, 1);
}

TEST_F(KVStoreTest, Get_NonExistentKey_ReturnsNullopt) {
    auto entry = store.Get("doesnotexist");
    EXPECT_FALSE(entry.has_value());
}

TEST_F(KVStoreTest, Put_SameKey_IncrementsVersion) {
    store.Put("key", "v1", 1);
    store.Put("key", "v2", 2);
    auto entry = store.Get("key");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->value, "v2");
    EXPECT_EQ(entry->version, 2);
}

TEST_F(KVStoreTest, Delete_ExistingKey_RemovesIt) {
    store.Put("key", "val", 1);
    ASSERT_TRUE(store.Delete("key", 2));
    EXPECT_FALSE(store.Get("key").has_value());
}

TEST_F(KVStoreTest, Delete_NonExistentKey_ReturnsFalse) {
    EXPECT_FALSE(store.Delete("ghost", 1));
}

TEST_F(KVStoreTest, Scan_PrefixMatch) {
    store.Put("/svc/web/node1", "10.0.0.1", 1);
    store.Put("/svc/web/node2", "10.0.0.2", 2);
    store.Put("/svc/db/node1",  "10.0.1.1", 3);
    store.Put("/config/app",    "value",     4);

    auto results = store.Scan("/svc/web/");
    EXPECT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].first, "/svc/web/node1");
    EXPECT_EQ(results[1].first, "/svc/web/node2");
}

TEST_F(KVStoreTest, Scan_WithLimit) {
    for (int i = 0; i < 10; ++i) {
        store.Put("/key/" + std::to_string(i), "v", i + 1);
    }
    auto results = store.Scan("/key/", 3);
    EXPECT_EQ(results.size(), 3u);
}

TEST_F(KVStoreTest, CAS_Success) {
    store.Put("k", "old", 1);
    ASSERT_TRUE(store.CAS("k", "old", "new", 2));
    auto e = store.Get("k");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->value, "new");
}

TEST_F(KVStoreTest, CAS_Failure_WrongExpected) {
    store.Put("k", "actual", 1);
    EXPECT_FALSE(store.CAS("k", "wrong", "new", 2));
    EXPECT_EQ(store.Get("k")->value, "actual"); // unchanged
}

TEST_F(KVStoreTest, Watch_FiresOnPut) {
    bool fired = false;
    std::string capturedKey;
    uint64_t wid = store.Watch("watched", false,
        [&](const std::string& key, const std::optional<KVEntry>& e) {
            fired      = true;
            capturedKey = key;
        });

    store.Put("watched", "hello", 1);
    EXPECT_TRUE(fired);
    EXPECT_EQ(capturedKey, "watched");
    store.Unwatch(wid);
}

TEST_F(KVStoreTest, Watch_PrefixFires_OnMatchingKey) {
    std::atomic<int> count{0};
    uint64_t wid = store.Watch("/svc/", true,
        [&](const std::string&, const std::optional<KVEntry>&) { ++count; });

    store.Put("/svc/web", "v1", 1);
    store.Put("/svc/db",  "v2", 2);
    store.Put("/other",   "v3", 3); // should not fire

    EXPECT_EQ(count.load(), 2);
    store.Unwatch(wid);
}

TEST_F(KVStoreTest, TTL_ExpiredKeyInvisible) {
    store.Put("ttl_key", "val", 1, std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(store.Get("ttl_key").has_value());
}

TEST_F(KVStoreTest, Snapshot_RoundTrip) {
    store.Put("a", "1", 1);
    store.Put("b", "2", 2);
    auto snap = store.TakeSnapshot();

    KVStore restored;
    restored.RestoreSnapshot(snap);
    EXPECT_EQ(restored.Get("a")->value, "1");
    EXPECT_EQ(restored.Get("b")->value, "2");
}

TEST_F(KVStoreTest, ConcurrentWrites_NoDataRace) {
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 100; ++i) {
                store.Put("key" + std::to_string(t * 100 + i), "val", t * 100 + i);
            }
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_EQ(store.Size(), 800u);
}
