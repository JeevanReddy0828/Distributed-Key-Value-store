#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <optional>

namespace orcdb {

struct PeerConfig {
    std::string id;
    std::string host;
    uint16_t    raftPort{2380};
};

struct RaftTimings {
    std::chrono::milliseconds electionTimeoutMin{150};
    std::chrono::milliseconds electionTimeoutMax{300};
    std::chrono::milliseconds heartbeatInterval{50};
    std::chrono::milliseconds rpcTimeout{50};
};

struct ServerConfig {
    std::string bindAddress{"0.0.0.0"};
    uint16_t    clientPort{2379};
    uint16_t    raftPort{2380};
    int         numIoThreads{4};
};

struct StorageConfig {
    std::string dataDir{"/var/lib/orcdb"};
    size_t      walMaxSegmentBytes{64 * 1024 * 1024}; // 64 MB
    bool        syncWrites{true};
};

struct AuthConfig {
    std::string jwtAlgorithm{"HS256"};
    std::string jwtSecret;
    std::string jwtPublicKeyPath;
    std::string jwtIssuer{"orcdb"};
};

struct NodeConfig {
    std::string   nodeId;
    ServerConfig  server;
    StorageConfig storage;
    RaftTimings   raft;
    AuthConfig    auth;
    std::vector<PeerConfig> peers;
    std::string   logLevel{"info"};
};

class Config {
public:
    static NodeConfig LoadFromFile(const std::string& path);
    static NodeConfig LoadFromEnv();

    // Merges: file values first, env overrides second
    static NodeConfig Load(const std::optional<std::string>& filePath = std::nullopt);
};

} // namespace orcdb
