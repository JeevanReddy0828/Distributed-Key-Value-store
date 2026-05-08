#include "Config.hpp"
#include <yaml-cpp/yaml.h>
#include <cstdlib>
#include <stdexcept>

namespace orcdb {

static std::string Env(const char* name, const std::string& def = "") {
    const char* v = std::getenv(name);
    return v ? std::string(v) : def;
}

NodeConfig Config::LoadFromFile(const std::string& path) {
    YAML::Node doc = YAML::LoadFile(path);
    NodeConfig cfg;

    cfg.nodeId                   = doc["node_id"].as<std::string>("node1");
    cfg.logLevel                 = doc["log_level"].as<std::string>("info");
    cfg.server.bindAddress       = doc["server"]["bind"].as<std::string>("0.0.0.0");
    cfg.server.clientPort        = doc["server"]["client_port"].as<uint16_t>(2379);
    cfg.server.raftPort          = doc["server"]["raft_port"].as<uint16_t>(2380);
    cfg.server.numIoThreads      = doc["server"]["io_threads"].as<int>(4);
    cfg.storage.dataDir          = doc["storage"]["data_dir"].as<std::string>("/var/lib/orcdb");
    cfg.storage.syncWrites       = doc["storage"]["sync_writes"].as<bool>(true);
    cfg.auth.jwtAlgorithm        = doc["auth"]["algorithm"].as<std::string>("HS256");
    cfg.auth.jwtSecret           = doc["auth"]["secret"].as<std::string>("");
    cfg.auth.jwtPublicKeyPath    = doc["auth"]["public_key_path"].as<std::string>("");
    cfg.auth.jwtIssuer           = doc["auth"]["issuer"].as<std::string>("orcdb");

    if (doc["raft"]) {
        auto r = doc["raft"];
        cfg.raft.electionTimeoutMin  = std::chrono::milliseconds(r["election_timeout_min_ms"].as<int>(150));
        cfg.raft.electionTimeoutMax  = std::chrono::milliseconds(r["election_timeout_max_ms"].as<int>(300));
        cfg.raft.heartbeatInterval   = std::chrono::milliseconds(r["heartbeat_interval_ms"].as<int>(50));
        cfg.raft.rpcTimeout          = std::chrono::milliseconds(r["rpc_timeout_ms"].as<int>(50));
    }

    if (doc["peers"]) {
        for (auto peer : doc["peers"]) {
            PeerConfig p;
            p.id       = peer["id"].as<std::string>();
            p.host     = peer["host"].as<std::string>();
            p.raftPort = peer["raft_port"].as<uint16_t>(2380);
            cfg.peers.push_back(p);
        }
    }

    return cfg;
}

NodeConfig Config::LoadFromEnv() {
    NodeConfig cfg;
    cfg.nodeId             = Env("NODE_ID", "node1");
    cfg.logLevel           = Env("LOG_LEVEL", "info");
    cfg.server.clientPort  = static_cast<uint16_t>(std::stoi(Env("CLIENT_PORT", "2379")));
    cfg.server.raftPort    = static_cast<uint16_t>(std::stoi(Env("RAFT_PORT", "2380")));
    cfg.storage.dataDir    = Env("DATA_DIR", "/var/lib/orcdb");
    cfg.auth.jwtSecret     = Env("JWT_SECRET", "");
    cfg.auth.jwtAlgorithm  = Env("JWT_ALGORITHM", "HS256");

    // PEERS=node2:2380,node3:2380
    std::string peers = Env("PEERS", "");
    if (!peers.empty()) {
        size_t pos = 0;
        int idx = 2;
        while (pos < peers.size()) {
            size_t comma = peers.find(',', pos);
            std::string entry = peers.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            size_t colon = entry.rfind(':');
            PeerConfig p;
            p.id       = "node" + std::to_string(idx++);
            p.host     = entry.substr(0, colon);
            p.raftPort = static_cast<uint16_t>(std::stoi(entry.substr(colon + 1)));
            cfg.peers.push_back(p);
            pos = (comma == std::string::npos) ? peers.size() : comma + 1;
        }
    }

    return cfg;
}

NodeConfig Config::Load(const std::optional<std::string>& filePath) {
    NodeConfig cfg = filePath ? LoadFromFile(*filePath) : LoadFromEnv();

    // Env always overrides file values for the fields docker-compose sets
    auto overrideStr = [](const char* name, std::string& field) {
        if (const char* v = std::getenv(name)) field = v;
    };
    auto overrideU16 = [](const char* name, uint16_t& field) {
        if (const char* v = std::getenv(name)) field = static_cast<uint16_t>(std::stoi(v));
    };

    overrideStr("NODE_ID",       cfg.nodeId);
    overrideStr("JWT_SECRET",    cfg.auth.jwtSecret);
    overrideStr("DATA_DIR",      cfg.storage.dataDir);
    overrideStr("LOG_LEVEL",     cfg.logLevel);
    overrideU16("CLIENT_PORT",   cfg.server.clientPort);
    overrideU16("RAFT_PORT",     cfg.server.raftPort);

    // PEERS env overrides peer list from file (format: "host1:port,host2:port")
    if (const char* peers = std::getenv("PEERS")) {
        cfg.peers.clear();
        std::string s(peers);
        size_t pos = 0, idx = 1;
        while (pos < s.size()) {
            size_t comma = s.find(',', pos);
            std::string entry = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            size_t colon = entry.rfind(':');
            PeerConfig p;
            p.host     = entry.substr(0, colon);
            p.id       = p.host; // use hostname as id
            p.raftPort = static_cast<uint16_t>(std::stoi(entry.substr(colon + 1)));
            cfg.peers.push_back(p);
            pos = (comma == std::string::npos) ? s.size() : comma + 1;
            ++idx;
        }
    }

    return cfg;
}

} // namespace orcdb
