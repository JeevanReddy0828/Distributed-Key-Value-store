#pragma once
#include "RaftConfig.hpp"
#include "RaftRPC.hpp"
#include "RaftLog.hpp"
#include "RaftState.hpp"
#include "../storage/KVStore.hpp"
#include "../storage/WriteAheadLog.hpp"
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <random>
#include <chrono>

namespace orcdb {

class RaftPeer;

struct ProposeResult {
    bool        ok{false};
    std::string error;
    uint64_t    index{0};
};

// Callback fired after a log entry is committed and applied to the state machine.
using ApplyCallback = std::function<void(const LogEntry&)>;

class RaftNode {
public:
    RaftNode(RaftConfig config,
             std::shared_ptr<KVStore>        store,
             std::shared_ptr<WriteAheadLog>  wal);
    ~RaftNode();

    void Start();
    void Stop();

    // Client write path: propose a KV operation; blocks until committed or error.
    // ttl_ms: key expiry in milliseconds from now, or -1 for no expiry.
    ProposeResult Propose(const std::string& key, const std::string& value, OpType op,
                          int64_t ttl_ms = -1,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));

    // RPC handlers (called by network layer on incoming Raft RPC frames)
    AppendEntriesResponse HandleAppendEntries(const AppendEntriesRequest& req);
    RequestVoteResponse   HandleRequestVote(const RequestVoteRequest& req);

    // Status queries
    bool        IsLeader() const;
    std::string GetLeaderId() const;
    RaftRole    GetRole() const;
    uint64_t    GetCurrentTerm() const;
    uint64_t    GetCommitIndex() const;

    void SetApplyCallback(ApplyCallback cb) { applyCallback_ = std::move(cb); }

private:
    // ── Election ────────────────────────────────────────────────────────────
    void ElectionLoop();
    void StartElection();
    void BecomeLeader();
    void BecomeFollower(uint64_t term, const std::string& leaderId = "");
    void BecomeCandidate();
    void ResetElectionTimer();
    std::chrono::milliseconds RandomElectionTimeout();

    // ── Replication ─────────────────────────────────────────────────────────
    void ReplicationLoop();
    void SendHeartbeatsToAll();
    void ReplicateToPeer(RaftPeer& peer, const std::string& peerId);

    // ── Apply ───────────────────────────────────────────────────────────────
    void ApplyLoop();
    void ApplyEntry(const LogEntry& entry);
    void AdvanceCommitIndex();

    // ── Persistence ─────────────────────────────────────────────────────────
    void PersistHardState();

    // ── Snapshot ────────────────────────────────────────────────────────────
    void MaybeSnapshot();

    // ── Helpers ─────────────────────────────────────────────────────────────
    size_t QuorumSize() const { return (peers_.size() + 1) / 2 + 1; }
    bool   IsLogUpToDate(uint64_t lastLogIndex, uint64_t lastLogTerm) const;

    // ── Config & Deps ────────────────────────────────────────────────────────
    RaftConfig                          config_;
    std::shared_ptr<KVStore>            store_;
    std::shared_ptr<WriteAheadLog>      wal_;
    std::shared_ptr<RaftLog>            log_;
    std::vector<std::unique_ptr<RaftPeer>> peers_;

    // ── Raft Persistent State ────────────────────────────────────────────────
    std::atomic<uint64_t>  currentTerm_{0};
    std::string            votedFor_;

    // ── Raft Volatile State ──────────────────────────────────────────────────
    std::atomic<uint64_t>  commitIndex_{0};
    std::atomic<uint64_t>  lastApplied_{0};
    std::atomic<RaftRole>  role_{RaftRole::Follower};
    std::string            leaderId_;

    // ── Leader volatile state (only valid when role == Leader) ───────────────
    std::unordered_map<std::string, uint64_t> nextIndex_;
    std::unordered_map<std::string, uint64_t> matchIndex_;

    // ── Concurrency ──────────────────────────────────────────────────────────
    mutable std::mutex      mu_;
    std::condition_variable electionCv_;   // woken when election timer should reset
    std::condition_variable applyCv_;      // woken when commitIndex advances
    std::condition_variable proposeCv_;    // woken when an entry is applied

    // ── Threads ──────────────────────────────────────────────────────────────
    std::atomic<bool>  running_{false};
    std::thread        electionThread_;
    std::thread        replicationThread_;
    std::thread        applyThread_;

    // ── Timing ───────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point lastHeartbeat_;
    std::mt19937_64                        rng_;

    // ── Callbacks ────────────────────────────────────────────────────────────
    ApplyCallback applyCallback_;
};

} // namespace orcdb
