#include <gtest/gtest.h>
#include "api/Router.hpp"
#include "api/KVHandler.hpp"
#include "api/HealthHandler.hpp"
#include "api/ClusterHandler.hpp"
#include "storage/KVStore.hpp"
#include "raft/RaftNode.hpp"
#include "raft/RaftConfig.hpp"
#include "storage/WriteAheadLog.hpp"
#include <filesystem>
#include <chrono>
#include <thread>

using namespace orcdb;
namespace fs = std::filesystem;

// Tests the API handler layer with a real single-node Raft (no quorum needed
// for no-op commits) backed by in-memory store.
class ApiCrudTest : public ::testing::Test {
protected:
    std::shared_ptr<KVStore>   store;
    std::shared_ptr<RaftNode>  raft;
    std::shared_ptr<Router>    router;
    std::string                dataDir;

    void SetUp() override {
        dataDir = "/tmp/orcdb_api_test_" + std::to_string(std::rand());
        fs::create_directories(dataDir);

        store = std::make_shared<KVStore>();
        auto wal = std::make_shared<WriteAheadLog>(dataDir + "/wal");

        RaftConfig cfg;
        cfg.nodeId             = "solo";
        cfg.electionTimeoutMin = std::chrono::milliseconds(100);
        cfg.electionTimeoutMax = std::chrono::milliseconds(200);
        cfg.heartbeatInterval  = std::chrono::milliseconds(50);
        // No peers → single-node cluster; becomes leader immediately

        raft = std::make_shared<RaftNode>(cfg, store, wal);
        raft->Start();

        // Wait for solo node to elect itself
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
        while (!raft->IsLeader() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        router = std::make_shared<Router>();

        HealthHandler  hh(raft); hh.RegisterRoutes(*router);
        KVHandler      kh(raft, store); kh.RegisterRoutes(*router);
        ClusterHandler ch(raft); ch.RegisterRoutes(*router);
    }

    void TearDown() override {
        raft->Stop();
        fs::remove_all(dataDir);
    }

    HttpResponse Dispatch(const std::string& method, const std::string& path,
                           const std::string& body = "") {
        HttpRequest req;
        req.method = method;
        req.path   = path;
        req.body   = body;
        if (!body.empty()) req.headers["Content-Length"] = std::to_string(body.size());

        AuthContext auth;
        auth.authenticated = true;
        auth.roles         = {"admin"};
        return router->Dispatch(req, auth);
    }
};

TEST_F(ApiCrudTest, HealthCheck_Returns200) {
    ASSERT_TRUE(raft->IsLeader()) << "Node did not become leader";
    auto resp = Dispatch("GET", "/healthz");
    EXPECT_EQ(resp.statusCode, 200);
}

TEST_F(ApiCrudTest, Put_NewKey_Returns201) {
    ASSERT_TRUE(raft->IsLeader());
    auto resp = Dispatch("PUT", "/v1/kv/mykey", R"({"value":"hello"})");
    EXPECT_EQ(resp.statusCode, 201);
}

TEST_F(ApiCrudTest, Get_ExistingKey_Returns200) {
    ASSERT_TRUE(raft->IsLeader());
    Dispatch("PUT", "/v1/kv/foo", R"({"value":"world"})");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto resp = Dispatch("GET", "/v1/kv/foo");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("world"), std::string::npos);
}

TEST_F(ApiCrudTest, Get_MissingKey_Returns404) {
    auto resp = Dispatch("GET", "/v1/kv/doesnotexist");
    EXPECT_EQ(resp.statusCode, 404);
}

TEST_F(ApiCrudTest, Delete_ExistingKey_Returns204) {
    ASSERT_TRUE(raft->IsLeader());
    Dispatch("PUT", "/v1/kv/delme", R"({"value":"x"})");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto resp = Dispatch("DELETE", "/v1/kv/delme");
    EXPECT_EQ(resp.statusCode, 204);
}

TEST_F(ApiCrudTest, ClusterStatus_ReturnsLeaderInfo) {
    ASSERT_TRUE(raft->IsLeader());
    auto resp = Dispatch("GET", "/v1/cluster/status");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("leader"), std::string::npos);
}

TEST_F(ApiCrudTest, ListKeys_WithPrefix) {
    ASSERT_TRUE(raft->IsLeader());
    Dispatch("PUT", "/v1/kv/svc:web:1", R"({"value":"a"})");
    Dispatch("PUT", "/v1/kv/svc:web:2", R"({"value":"b"})");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    HttpRequest req;
    req.method = "GET";
    req.path   = "/v1/kv";
    req.queryParams["prefix"] = "svc:web";
    AuthContext auth; auth.authenticated = true; auth.roles = {"admin"};
    auto resp = router->Dispatch(req, auth);

    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("svc:web"), std::string::npos);
}

TEST_F(ApiCrudTest, Put_BadJson_Returns400) {
    auto resp = Dispatch("PUT", "/v1/kv/bad", "not json");
    EXPECT_EQ(resp.statusCode, 400);
}
