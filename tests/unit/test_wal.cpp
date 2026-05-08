#include <gtest/gtest.h>
#include "storage/WriteAheadLog.hpp"
#include <filesystem>
#include <cstdlib>

using namespace orcdb;
namespace fs = std::filesystem;

class WalTest : public ::testing::Test {
protected:
    std::string testDir;

    void SetUp() override {
        testDir = "/tmp/orcdb_wal_test_" + std::to_string(std::rand());
        fs::create_directories(testDir);
    }

    void TearDown() override {
        fs::remove_all(testDir);
    }
};

TEST_F(WalTest, AppendAndRecover_SingleEntry) {
    {
        WriteAheadLog wal(testDir);
        WriteAheadLog::RawLogEntry e;
        e.term    = 1;
        e.index   = 1;
        e.command = R"({"op":1,"key":"foo","value":"bar"})";
        wal.AppendLogEntry(e);
        wal.Sync();
    }
    // Recover in new instance
    WriteAheadLog wal2(testDir);
    auto result = wal2.Recover();
    ASSERT_EQ(result.entries.size(), 1u);
    EXPECT_EQ(result.entries[0].term,  1u);
    EXPECT_EQ(result.entries[0].index, 1u);
}

TEST_F(WalTest, HardState_Persisted_AndRecovered) {
    {
        WriteAheadLog wal(testDir);
        WriteAheadLog::HardState hs;
        hs.term     = 5;
        hs.votedFor = "node2";
        wal.WriteHardState(hs);
        wal.Sync();
    }
    WriteAheadLog wal2(testDir);
    auto result = wal2.Recover();
    EXPECT_EQ(result.hardState.term,     5u);
    EXPECT_EQ(result.hardState.votedFor, "node2");
}

TEST_F(WalTest, MultipleEntries_RecoveredInOrder) {
    {
        WriteAheadLog wal(testDir);
        for (uint64_t i = 1; i <= 10; ++i) {
            WriteAheadLog::RawLogEntry e;
            e.term    = 1;
            e.index   = i;
            e.command = R"({"op":1,"key":"k","value":"v"})";
            wal.AppendLogEntry(e);
        }
        wal.Sync();
    }
    WriteAheadLog wal2(testDir);
    auto result = wal2.Recover();
    ASSERT_EQ(result.entries.size(), 10u);
    for (uint64_t i = 0; i < 10; ++i) {
        EXPECT_EQ(result.entries[i].index, i + 1);
    }
}

TEST_F(WalTest, Snapshot_ClearsOldEntries) {
    {
        WriteAheadLog wal(testDir);
        for (uint64_t i = 1; i <= 5; ++i) {
            WriteAheadLog::RawLogEntry e;
            e.term = 1; e.index = i;
            e.command = "{}";
            wal.AppendLogEntry(e);
        }
        wal.WriteSnapshot(5, 1, R"([{"key":"foo","value":"bar","version":1,"raft_index":5,"expires_ms":-1}])");
        wal.Sync();
    }
    WriteAheadLog wal2(testDir);
    auto result = wal2.Recover();
    EXPECT_EQ(result.snapshotIndex, 5u);
    ASSERT_TRUE(result.latestSnapshot.has_value());
    EXPECT_TRUE(result.entries.empty()); // entries before snapshot are discarded
}
