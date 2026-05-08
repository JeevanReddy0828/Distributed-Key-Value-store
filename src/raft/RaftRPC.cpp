#include "RaftRPC.hpp"
#include <nlohmann/json.hpp>
#include <cstring>
#include <stdexcept>

namespace orcdb {

using json = nlohmann::json;

// ── LogEntry ───────────────────────────────────────────────────────────────
LogEntry LogEntry::NoOp(uint64_t term, uint64_t index) {
    LogEntry e;
    e.term    = term;
    e.index   = index;
    e.opType  = OpType::NoOp;
    e.command = json{{"op", "noop"}}.dump();
    return e;
}

std::string LogEntry::Serialize() const {
    return json{
        {"term",    term},
        {"index",   index},
        {"op",      static_cast<int>(opType)},
        {"key",     key},
        {"value",   value},
        {"ttl_ms",  ttl_ms},
        {"command", command}
    }.dump();
}

LogEntry LogEntry::Deserialize(const std::string& s) {
    auto j = json::parse(s);
    LogEntry e;
    e.term    = j["term"].get<uint64_t>();
    e.index   = j["index"].get<uint64_t>();
    e.opType  = static_cast<OpType>(j["op"].get<int>());
    e.key     = j.value("key", "");
    e.value   = j.value("value", "");
    e.ttl_ms  = j.value("ttl_ms", int64_t{-1});
    e.command = j.value("command", "");
    return e;
}

// ── AppendEntriesRequest ───────────────────────────────────────────────────
std::string AppendEntriesRequest::Serialize() const {
    json entriesJson = json::array();
    for (auto& e : entries) entriesJson.push_back(json::parse(e.Serialize()));
    return json{
        {"term",          term},
        {"leader_id",     leaderId},
        {"prev_log_index",prevLogIndex},
        {"prev_log_term", prevLogTerm},
        {"entries",       entriesJson},
        {"leader_commit", leaderCommit}
    }.dump();
}

AppendEntriesRequest AppendEntriesRequest::Deserialize(const std::string& s) {
    auto j = json::parse(s);
    AppendEntriesRequest r;
    r.term         = j["term"].get<uint64_t>();
    r.leaderId     = j["leader_id"].get<std::string>();
    r.prevLogIndex = j["prev_log_index"].get<uint64_t>();
    r.prevLogTerm  = j["prev_log_term"].get<uint64_t>();
    r.leaderCommit = j["leader_commit"].get<uint64_t>();
    for (auto& e : j["entries"]) r.entries.push_back(LogEntry::Deserialize(e.dump()));
    return r;
}

// ── AppendEntriesResponse ──────────────────────────────────────────────────
std::string AppendEntriesResponse::Serialize() const {
    return json{{"term", term}, {"success", success},
                {"conflict_index", conflictIndex}, {"conflict_term", conflictTerm}}.dump();
}

AppendEntriesResponse AppendEntriesResponse::Deserialize(const std::string& s) {
    auto j = json::parse(s);
    AppendEntriesResponse r;
    r.term          = j["term"].get<uint64_t>();
    r.success       = j["success"].get<bool>();
    r.conflictIndex = j.value("conflict_index", uint64_t{0});
    r.conflictTerm  = j.value("conflict_term",  uint64_t{0});
    return r;
}

// ── RequestVoteRequest ─────────────────────────────────────────────────────
std::string RequestVoteRequest::Serialize() const {
    return json{{"term", term}, {"candidate_id", candidateId},
                {"last_log_index", lastLogIndex}, {"last_log_term", lastLogTerm}}.dump();
}

RequestVoteRequest RequestVoteRequest::Deserialize(const std::string& s) {
    auto j = json::parse(s);
    RequestVoteRequest r;
    r.term         = j["term"].get<uint64_t>();
    r.candidateId  = j["candidate_id"].get<std::string>();
    r.lastLogIndex = j["last_log_index"].get<uint64_t>();
    r.lastLogTerm  = j["last_log_term"].get<uint64_t>();
    return r;
}

// ── RequestVoteResponse ────────────────────────────────────────────────────
std::string RequestVoteResponse::Serialize() const {
    return json{{"term", term}, {"vote_granted", voteGranted}}.dump();
}

RequestVoteResponse RequestVoteResponse::Deserialize(const std::string& s) {
    auto j = json::parse(s);
    RequestVoteResponse r;
    r.term        = j["term"].get<uint64_t>();
    r.voteGranted = j["vote_granted"].get<bool>();
    return r;
}

// ── RpcFrame ───────────────────────────────────────────────────────────────
std::vector<uint8_t> RpcFrame::Encode() const {
    uint32_t payloadLen = static_cast<uint32_t>(payload.size());
    // [type 1][seq 4][len 4][payload]
    std::vector<uint8_t> buf(9 + payloadLen);
    buf[0] = static_cast<uint8_t>(type);
    std::memcpy(buf.data() + 1, &seq,        4);
    std::memcpy(buf.data() + 5, &payloadLen, 4);
    std::memcpy(buf.data() + 9, payload.data(), payloadLen);
    return buf;
}

bool RpcFrame::Decode(const uint8_t* data, size_t len, RpcFrame& out, size_t& consumed) {
    if (len < 9) return false;
    uint32_t payloadLen{};
    std::memcpy(&payloadLen, data + 5, 4);
    if (len < 9 + payloadLen) return false;

    out.type = static_cast<RpcType>(data[0]);
    std::memcpy(&out.seq, data + 1, 4);
    out.payload.assign(reinterpret_cast<const char*>(data + 9), payloadLen);
    consumed = 9 + payloadLen;
    return true;
}

} // namespace orcdb
