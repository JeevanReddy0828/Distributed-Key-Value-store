#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace orcdb {

struct RaftPeerInfo {
    std::string id;
    std::string host;
    uint16_t    port{2380};
};

struct RaftConfig {
    std::string              nodeId;
    std::vector<RaftPeerInfo> peers;
    uint16_t                 raftPort{2380};

    std::chrono::milliseconds electionTimeoutMin{150};
    std::chrono::milliseconds electionTimeoutMax{300};
    std::chrono::milliseconds heartbeatInterval{50};
    std::chrono::milliseconds rpcTimeout{50};

    // Log compaction: take snapshot when log exceeds this many entries
    size_t snapshotThreshold{10000};
};

} // namespace orcdb
