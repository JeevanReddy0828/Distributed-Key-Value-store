#include "util/Logger.hpp"
#include "util/Config.hpp"
#include "storage/KVStore.hpp"
#include "storage/WriteAheadLog.hpp"
#include "raft/RaftConfig.hpp"
#include "raft/RaftNode.hpp"
#include "raft/RaftRpcServer.hpp"
#include "network/TcpServer.hpp"
#include "api/Router.hpp"
#include "api/KVHandler.hpp"
#include "api/WatchHandler.hpp"
#include "api/ClusterHandler.hpp"
#include "api/HealthHandler.hpp"
#include "auth/JwtValidator.hpp"
#include "auth/AuthMiddleware.hpp"
#include "auth/TokenStore.hpp"
#include "metrics/MetricsHandler.hpp"
#include "metrics/OrcMetrics.hpp"

#include <csignal>
#include <atomic>
#include <iostream>
#include <thread>
#include <chrono>

namespace {
std::atomic<bool> gShutdown{false};
}

static void SignalHandler(int) { gShutdown = true; }

int main(int argc, char* argv[]) {
    // ── Config ────────────────────────────────────────────────────────────────
    std::optional<std::string> configPath;
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--config") configPath = argv[i + 1];
    }
    auto cfg = orcdb::Config::Load(configPath);

    // ── Logging ───────────────────────────────────────────────────────────────
    orcdb::Logger::Instance().SetNodeId(cfg.nodeId);
    if      (cfg.logLevel == "debug") orcdb::Logger::Instance().SetLevel(orcdb::LogLevel::Debug);
    else if (cfg.logLevel == "warn")  orcdb::Logger::Instance().SetLevel(orcdb::LogLevel::Warn);
    else if (cfg.logLevel == "error") orcdb::Logger::Instance().SetLevel(orcdb::LogLevel::Error);
    else                               orcdb::Logger::Instance().SetLevel(orcdb::LogLevel::Info);

    LOG_INFOF("OrcDB v0.1.0 starting — node_id={}", cfg.nodeId);

    // ── Storage ───────────────────────────────────────────────────────────────
    auto wal   = std::make_shared<orcdb::WriteAheadLog>(cfg.storage.dataDir + "/wal");
    auto store = std::make_shared<orcdb::KVStore>();

    // ── Raft ─────────────────────────────────────────────────────────────────
    orcdb::RaftConfig raftCfg;
    raftCfg.nodeId               = cfg.nodeId;
    raftCfg.raftPort             = cfg.server.raftPort;
    raftCfg.electionTimeoutMin   = cfg.raft.electionTimeoutMin;
    raftCfg.electionTimeoutMax   = cfg.raft.electionTimeoutMax;
    raftCfg.heartbeatInterval    = cfg.raft.heartbeatInterval;
    raftCfg.rpcTimeout           = cfg.raft.rpcTimeout;
    for (auto& p : cfg.peers) {
        orcdb::RaftPeerInfo pi;
        pi.id   = p.id;
        pi.host = p.host;
        pi.port = p.raftPort;
        raftCfg.peers.push_back(pi);
    }

    auto raft = std::make_shared<orcdb::RaftNode>(raftCfg, store, wal);

    // Wire Raft metrics
    raft->SetApplyCallback([](const orcdb::LogEntry& e) {
        orcdb::OrcMetrics::Instance().RaftCommittedEntries().Inc();
        if (e.opType == orcdb::OpType::Put)    orcdb::OrcMetrics::Instance().KVPuts().Inc();
        if (e.opType == orcdb::OpType::Delete) orcdb::OrcMetrics::Instance().KVDeletes().Inc();
    });

    raft->Start();

    // ── Raft RPC server (listens for incoming AppendEntries / RequestVote) ────
    orcdb::RaftRpcServer raftRpc(cfg.server.raftPort, raft);
    raftRpc.Start();

    // ── Auth ──────────────────────────────────────────────────────────────────
    orcdb::JwtValidator::Config jwtCfg;
    jwtCfg.algorithm = cfg.auth.jwtAlgorithm;
    jwtCfg.secret    = cfg.auth.jwtSecret;
    jwtCfg.issuer    = cfg.auth.jwtIssuer;

    auto validator  = std::make_shared<orcdb::JwtValidator>(jwtCfg);
    auto tokenStore = std::make_shared<orcdb::TokenStore>();
    auto authMw     = std::make_shared<orcdb::AuthMiddleware>(validator, tokenStore);

    // ── Router + Handlers ─────────────────────────────────────────────────────
    auto router = std::make_shared<orcdb::Router>();
    router->SetAuthMiddleware(authMw->MakeMiddleware());

    orcdb::HealthHandler  healthHandler(raft);
    orcdb::KVHandler      kvHandler(raft, store);
    orcdb::WatchHandler   watchHandler(store);
    orcdb::ClusterHandler clusterHandler(raft);
    orcdb::MetricsHandler metricsHandler;

    healthHandler.RegisterRoutes(*router);
    kvHandler.RegisterRoutes(*router);
    watchHandler.RegisterRoutes(*router);
    clusterHandler.RegisterRoutes(*router);
    metricsHandler.RegisterRoutes(*router);

    // ── HTTP Server ───────────────────────────────────────────────────────────
    orcdb::TcpServer::Config serverCfg;
    serverCfg.bindAddress  = cfg.server.bindAddress;
    serverCfg.port         = cfg.server.clientPort;
    serverCfg.numIoThreads = cfg.server.numIoThreads;

    orcdb::TcpServer server(serverCfg, [&router](orcdb::HttpRequest& req) -> orcdb::HttpResponse {
        orcdb::OrcMetrics::Instance().HttpRequestsTotal().Inc();
        orcdb::AuthContext auth;
        return router->Dispatch(req, auth);
    });
    server.Start();

    // ── Signal handling ───────────────────────────────────────────────────────
    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    LOG_INFOF("Listening on port {}, raft port {}",
              cfg.server.clientPort, cfg.server.raftPort);

    while (!gShutdown) {
        // Update live metrics periodically
        orcdb::OrcMetrics::Instance().RaftCurrentTerm().Set(
            static_cast<double>(raft->GetCurrentTerm()));
        orcdb::OrcMetrics::Instance().RaftIsLeader().Set(raft->IsLeader() ? 1.0 : 0.0);
        orcdb::OrcMetrics::Instance().KVStoreSize().Set(
            static_cast<double>(store->Size()));
        orcdb::OrcMetrics::Instance().ActiveConnections().Set(
            static_cast<double>(server.ActiveConnections()));

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    LOG_INFO("Shutting down...");
    server.Stop();
    raftRpc.Stop();
    raft->Stop();
    LOG_INFO("OrcDB stopped.");
    return 0;
}
