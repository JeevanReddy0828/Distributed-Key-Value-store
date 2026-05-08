#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace orcdb {

// ── Log entry ──────────────────────────────────────────────────────────────
enum class OpType : uint8_t { Put = 1, Delete = 2, NoOp = 3 };

struct LogEntry {
    uint64_t    term{0};
    uint64_t    index{0};
    OpType      opType{OpType::NoOp};
    std::string key;
    std::string value;
    int64_t     ttl_ms{-1}; // -1 means no TTL
    std::string command; // full JSON for WAL

    static LogEntry NoOp(uint64_t term, uint64_t index);
    std::string Serialize() const;
    static LogEntry Deserialize(const std::string& json);
};

// ── AppendEntries ──────────────────────────────────────────────────────────
struct AppendEntriesRequest {
    uint64_t              term{0};
    std::string           leaderId;
    uint64_t              prevLogIndex{0};
    uint64_t              prevLogTerm{0};
    std::vector<LogEntry> entries;
    uint64_t              leaderCommit{0};

    std::string Serialize() const;
    static AppendEntriesRequest Deserialize(const std::string& json);
};

struct AppendEntriesResponse {
    uint64_t term{0};
    bool     success{false};
    uint64_t conflictIndex{0};
    uint64_t conflictTerm{0};

    std::string Serialize() const;
    static AppendEntriesResponse Deserialize(const std::string& json);
};

// ── RequestVote ────────────────────────────────────────────────────────────
struct RequestVoteRequest {
    uint64_t    term{0};
    std::string candidateId;
    uint64_t    lastLogIndex{0};
    uint64_t    lastLogTerm{0};

    std::string Serialize() const;
    static RequestVoteRequest Deserialize(const std::string& json);
};

struct RequestVoteResponse {
    uint64_t term{0};
    bool     voteGranted{false};

    std::string Serialize() const;
    static RequestVoteResponse Deserialize(const std::string& json);
};

// ── Wire framing over TCP: [type 1B][seq 4B][length 4B][payload] ──────────
enum class RpcType : uint8_t {
    AppendEntriesReq  = 0x01,
    AppendEntriesResp = 0x02,
    RequestVoteReq    = 0x03,
    RequestVoteResp   = 0x04,
};

struct RpcFrame {
    RpcType     type;
    uint32_t    seq{0};
    std::string payload;

    std::vector<uint8_t> Encode() const;
    // Returns false if buffer too short (partial frame)
    static bool Decode(const uint8_t* data, size_t len, RpcFrame& out, size_t& consumed);
};

} // namespace orcdb
