#include <gtest/gtest.h>
#include "raft/RaftNode.hpp"
#include "raft/RaftConfig.hpp"
#include "storage/KVStore.hpp"
#include "storage/WriteAheadLog.hpp"
#include <memory>
#include <thread>
#include <chrono>
#include <filesystem>

using namespace orcdb;
namespace fs = std::filesystem;

// Creates a 3-node Raft cluster running in-process.
// Each node communicates over real TCP on localhost with assigned ports.
struct ClusterNode {
    std::shared_ptr<KVStore>   store;
    std::shared_ptr<RaftNode>  raft;
    std::string                nodeId;
    std::string                dataDir;
};

class ThreeNodeClusterTest : public ::testing::Test {
protected:
    static constexpr int BASE_PORT = 19100;
    std::vector<ClusterNode> nodes;

    void SetUp() override {
        std::vector<std::string> ids   = {"n1", "n2", "n3"};
        std::vector<uint16_t>    ports = {
            BASE_PORT, BASE_PORT + 1, BASE_PORT + 2
        };

        for (int i = 0; i < 3; ++i) {
            ClusterNode cn;
            cn.nodeId  = ids[i];
            cn.dataDir = "/tmp/orcdb_cluster_" + ids[i] + "_" + std::to_string(std::rand());
            fs::create_directories(cn.dataDir);

            cn.store = std::make_shared<KVStore>();
            auto wal = std::make_shared<WriteAheadLog>(cn.dataDir + "/wal");

            RaftConfig cfg;
            cfg.nodeId              = ids[i];
            cfg.raftPort            = ports[i];
            cfg.electionTimeoutMin  = std::chrono::milliseconds(150);
            cfg.electionTimeoutMax  = std::chrono::milliseconds(300);
            cfg.heartbeatInterval   = std::chrono::milliseconds(50);
            cfg.rpcTimeout          = std::chrono::milliseconds(50);

            // Add peer info for the other two nodes
            for (int j = 0; j < 3; ++j) {
                if (j == i) continue;
                RaftPeerInfo pi;
                pi.id   = ids[j];
                pi.host = "127.0.0.1";
                pi.port = ports[j];
                cfg.peers.push_back(pi);
            }

            cn.raft = std::make_shared<RaftNode>(cfg, cn.store, wal);
            cn.raft->Start();
            nodes.push_back(std::move(cn));
        }
    }

    void TearDown() override {
        for (auto& n : nodes) {
            n.raft->Stop();
            fs::remove_all(n.dataDir);
        }
        nodes.clear();
    }

    ClusterNode* WaitForLeader(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            for (auto& n : nodes) {
                if (n.raft->IsLeader()) return &n;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return nullptr;
    }
};

TEST_F(ThreeNodeClusterTest, ElectsLeaderWithinTimeout) {
    auto* leader = WaitForLeader();
    ASSERT_NE(leader, nullptr) << "No leader elected within 3s";
    EXPECT_TRUE(leader->raft->IsLeader());
    EXPECT_GT(leader->raft->GetCurrentTerm(), 0u);
}

TEST_F(ThreeNodeClusterTest, ExactlyOneLeaderAtATime) {
    WaitForLeader();
    int leaderCount = 0;
    for (auto& n : nodes) {
        if (n.raft->IsLeader()) ++leaderCount;
    }
    EXPECT_EQ(leaderCount, 1);
}

TEST_F(ThreeNodeClusterTest, AllNodesAgreeOnTerm) {
    auto* leader = WaitForLeader();
    ASSERT_NE(leader, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    uint64_t term = leader->raft->GetCurrentTerm();
    for (auto& n : nodes) {
        EXPECT_EQ(n.raft->GetCurrentTerm(), term)
            << "Node " << n.nodeId << " has mismatched term";
    }
}

TEST_F(ThreeNodeClusterTest, WriteAndReadKeyViaLeader) {
    auto* leader = WaitForLeader();
    ASSERT_NE(leader, nullptr);

    auto result = leader->raft->Propose("test-key", "test-value", OpType::Put,
                                         std::chrono::milliseconds(2000));
    ASSERT_TRUE(result.ok) << "Propose failed: " << result.error;

    // Wait for replication
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Value should be visible on all nodes' stores
    for (auto& n : nodes) {
        auto entry = n.store->Get("test-key");
        ASSERT_TRUE(entry.has_value())
            << "Node " << n.nodeId << " missing key after replication";
        EXPECT_EQ(entry->value, "test-value");
    }
}

TEST_F(ThreeNodeClusterTest, CommitIndex_AdvancesAfterWrite) {
    auto* leader = WaitForLeader();
    ASSERT_NE(leader, nullptr);

    uint64_t commitBefore = leader->raft->GetCommitIndex();
    leader->raft->Propose("k", "v", OpType::Put, std::chrono::milliseconds(2000));
    uint64_t commitAfter = leader->raft->GetCommitIndex();

    EXPECT_GT(commitAfter, commitBefore);
}
