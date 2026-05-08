#include "RaftNode.hpp"
#include "RaftPeer.hpp"
#include "../util/Logger.hpp"
#include <algorithm>
#include <stdexcept>

namespace orcdb {

RaftNode::RaftNode(RaftConfig config,
                   std::shared_ptr<KVStore>       store,
                   std::shared_ptr<WriteAheadLog> wal)
    : config_(std::move(config))
    , store_(std::move(store))
    , wal_(std::move(wal))
    , rng_(std::chrono::steady_clock::now().time_since_epoch().count())
{
    log_ = std::make_shared<RaftLog>(wal_);

    // Build peer connections
    for (auto& pi : config_.peers) {
        peers_.push_back(std::make_unique<RaftPeer>(pi.id, pi.host, pi.port));
    }

    // Recover persisted state from WAL
    if (wal_) {
        auto recovery = wal_->Recover();
        currentTerm_.store(recovery.hardState.term);
        votedFor_ = recovery.hardState.votedFor;

        if (recovery.latestSnapshot) {
            store_->RestoreSnapshot(*recovery.latestSnapshot);
            log_->SetSnapshot(recovery.snapshotIndex, recovery.snapshotTerm);
            lastApplied_.store(recovery.snapshotIndex);
            commitIndex_.store(recovery.snapshotIndex);
        }
        for (auto& raw : recovery.entries) {
            LogEntry e = LogEntry::Deserialize(raw.command);
            log_->Append(e);
        }
    }

    lastHeartbeat_ = std::chrono::steady_clock::now();
}

RaftNode::~RaftNode() { Stop(); }

void RaftNode::Start() {
    running_ = true;
    electionThread_     = std::thread([this]{ ElectionLoop(); });
    replicationThread_  = std::thread([this]{ ReplicationLoop(); });
    applyThread_        = std::thread([this]{ ApplyLoop(); });
    LOG_INFOF("[{}] Raft node started (term={})", config_.nodeId, currentTerm_.load());
}

void RaftNode::Stop() {
    if (!running_.exchange(false)) return;
    electionCv_.notify_all();
    applyCv_.notify_all();
    proposeCv_.notify_all();
    if (electionThread_.joinable())    electionThread_.join();
    if (replicationThread_.joinable()) replicationThread_.join();
    if (applyThread_.joinable())       applyThread_.join();
    LOG_INFO("Raft node stopped");
}

// ── Election ────────────────────────────────────────────────────────────────

std::chrono::milliseconds RaftNode::RandomElectionTimeout() {
    std::uniform_int_distribution<int64_t> dist(
        config_.electionTimeoutMin.count(),
        config_.electionTimeoutMax.count());
    return std::chrono::milliseconds(dist(rng_));
}

void RaftNode::ResetElectionTimer() {
    lastHeartbeat_ = std::chrono::steady_clock::now();
    electionCv_.notify_all();
}

void RaftNode::ElectionLoop() {
    while (running_) {
        auto timeout = RandomElectionTimeout();
        std::unique_lock<std::mutex> lock(mu_);

        // Wait until election timeout expires without a heartbeat
        bool timedOut = !electionCv_.wait_for(lock, timeout, [this, &timeout]{
            auto elapsed = std::chrono::steady_clock::now() - lastHeartbeat_;
            return !running_ || elapsed >= timeout || role_ == RaftRole::Leader;
        });
        (void)timedOut;

        if (!running_) break;
        if (role_ == RaftRole::Leader) {
            // Leaders don't need election timer
            electionCv_.wait(lock, [this]{
                return !running_ || role_ != RaftRole::Leader;
            });
            continue;
        }

        auto elapsed = std::chrono::steady_clock::now() - lastHeartbeat_;
        if (elapsed >= timeout) {
            lock.unlock();
            StartElection();
        }
    }
}

void RaftNode::StartElection() {
    uint64_t term;
    uint64_t lastIndex, lastTerm;
    {
        std::lock_guard<std::mutex> lock(mu_);
        currentTerm_++;
        term = currentTerm_;
        votedFor_ = config_.nodeId;
        role_     = RaftRole::Candidate;
        PersistHardState();
        lastIndex = log_->LastIndex();
        lastTerm  = log_->LastTerm();
        lastHeartbeat_ = std::chrono::steady_clock::now();
    }

    LOG_INFOF("[{}] Starting election for term {}", config_.nodeId, term);

    RequestVoteRequest req;
    req.term         = term;
    req.candidateId  = config_.nodeId;
    req.lastLogIndex = lastIndex;
    req.lastLogTerm  = lastTerm;

    std::atomic<int> votes{1}; // Vote for self
    std::atomic<int> responded{0};
    size_t peerCount = peers_.size();

    std::vector<std::future<RequestVoteResponse>> futures;
    for (auto& peer : peers_) {
        futures.push_back(peer->SendRequestVote(req, config_.rpcTimeout));
    }

    for (size_t i = 0; i < futures.size(); ++i) {
        try {
            auto& f = futures[i];
            if (f.wait_for(config_.rpcTimeout) == std::future_status::ready) {
                auto resp = f.get();
                std::lock_guard<std::mutex> lock(mu_);
                ++responded;
                if (resp.term > currentTerm_) {
                    BecomeFollower(resp.term);
                    return;
                }
                if (resp.voteGranted && currentTerm_ == term && role_ == RaftRole::Candidate) {
                    ++votes;
                }
            }
        } catch (...) {}
    }

    std::lock_guard<std::mutex> lock(mu_);
    if (role_ != RaftRole::Candidate || currentTerm_ != term) return;

    if (static_cast<size_t>(votes.load()) >= QuorumSize()) {
        BecomeLeader();
    }
    // else: split vote; will retry after next election timeout
}

void RaftNode::BecomeLeader() {
    // Called with mu_ held
    role_     = RaftRole::Leader;
    leaderId_ = config_.nodeId;

    uint64_t lastIndex = log_->LastIndex();
    for (auto& peer : peers_) {
        nextIndex_[peer->Id()]  = lastIndex + 1;
        matchIndex_[peer->Id()] = 0;
    }

    LOG_INFOF("[{}] Became LEADER for term {}", config_.nodeId, currentTerm_.load());

    // Append a no-op to commit entries from prior terms
    LogEntry noop = LogEntry::NoOp(currentTerm_, lastIndex + 1);
    log_->Append(noop);
}

void RaftNode::BecomeFollower(uint64_t term, const std::string& leaderId) {
    // Called with mu_ held
    currentTerm_.store(term);
    role_     = RaftRole::Follower;
    votedFor_ = "";
    if (!leaderId.empty()) leaderId_ = leaderId;
    PersistHardState();
    ResetElectionTimer();
}

void RaftNode::PersistHardState() {
    if (wal_) {
        WriteAheadLog::HardState hs;
        hs.term     = currentTerm_;
        hs.votedFor = votedFor_;
        wal_->WriteHardState(hs);
    }
}

// ── Replication ─────────────────────────────────────────────────────────────

void RaftNode::ReplicationLoop() {
    while (running_) {
        std::this_thread::sleep_for(config_.heartbeatInterval);
        if (role_ == RaftRole::Leader) {
            SendHeartbeatsToAll();
        }
    }
}

void RaftNode::SendHeartbeatsToAll() {
    for (auto& peer : peers_) {
        ReplicateToPeer(*peer, peer->Id());
    }
}

void RaftNode::ReplicateToPeer(RaftPeer& peer, const std::string& peerId) {
    uint64_t next, prevIndex, prevTerm, leaderCommit, currentTerm;
    std::vector<LogEntry> entries;

    {
        std::lock_guard<std::mutex> lock(mu_);
        if (role_ != RaftRole::Leader) return;
        currentTerm  = currentTerm_;
        leaderCommit = commitIndex_;
        next         = nextIndex_.count(peerId) ? nextIndex_[peerId] : log_->LastIndex() + 1;
        prevIndex    = next - 1;
        prevTerm     = log_->GetTerm(prevIndex);

        // Send up to 100 entries per RPC
        uint64_t lastIndex = log_->LastIndex();
        for (uint64_t i = next; i <= lastIndex && i < next + 100; ++i) {
            try { entries.push_back(log_->GetEntry(i)); }
            catch (...) { break; }
        }
    }

    AppendEntriesRequest req;
    req.term         = currentTerm;
    req.leaderId     = config_.nodeId;
    req.prevLogIndex = prevIndex;
    req.prevLogTerm  = prevTerm;
    req.entries      = entries;
    req.leaderCommit = leaderCommit;

    try {
        auto f = peer.SendAppendEntries(req, config_.rpcTimeout);
        if (f.wait_for(config_.rpcTimeout) != std::future_status::ready) return;
        auto resp = f.get();

        std::lock_guard<std::mutex> lock(mu_);
        if (resp.term > currentTerm_) {
            BecomeFollower(resp.term);
            return;
        }
        if (role_ != RaftRole::Leader || resp.term != currentTerm_) return;

        if (resp.success) {
            uint64_t newMatch = prevIndex + entries.size();
            matchIndex_[peerId] = std::max(matchIndex_[peerId], newMatch);
            nextIndex_[peerId]  = matchIndex_[peerId] + 1;
            AdvanceCommitIndex();
        } else {
            // Back up nextIndex using conflict hint
            if (resp.conflictTerm > 0) {
                uint64_t lastInTerm = 0;
                for (uint64_t i = log_->LastIndex(); i > log_->SnapshotIndex(); --i) {
                    if (log_->GetTerm(i) == resp.conflictTerm) { lastInTerm = i; break; }
                }
                nextIndex_[peerId] = lastInTerm > 0 ? lastInTerm + 1 : resp.conflictIndex;
            } else {
                nextIndex_[peerId] = resp.conflictIndex > 0 ? resp.conflictIndex : 1;
            }
        }
    } catch (...) {}
}

void RaftNode::AdvanceCommitIndex() {
    // Called with mu_ held
    uint64_t lastIndex = log_->LastIndex();
    for (uint64_t n = lastIndex; n > commitIndex_; --n) {
        if (log_->GetTerm(n) != currentTerm_) continue;
        size_t replicated = 1; // leader self
        for (auto& [id, match] : matchIndex_) {
            if (match >= n) ++replicated;
        }
        if (replicated >= QuorumSize()) {
            commitIndex_.store(n);
            applyCv_.notify_all();
            proposeCv_.notify_all();
            break;
        }
    }
}

// ── Apply ────────────────────────────────────────────────────────────────────

void RaftNode::ApplyLoop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(mu_);
        applyCv_.wait(lock, [this]{
            return !running_ || lastApplied_ < commitIndex_;
        });
        if (!running_) break;

        while (lastApplied_ < commitIndex_) {
            uint64_t next = lastApplied_ + 1;
            LogEntry entry;
            try { entry = log_->GetEntry(next); }
            catch (...) { break; }
            lock.unlock();
            ApplyEntry(entry);
            lock.lock();
            lastApplied_.store(next);
            proposeCv_.notify_all();
        }

        MaybeSnapshot();
    }
}

void RaftNode::ApplyEntry(const LogEntry& entry) {
    if (entry.opType == OpType::NoOp) return;

    if (entry.opType == OpType::Put) {
        std::optional<std::chrono::milliseconds> ttl;
        if (entry.ttl_ms > 0) ttl = std::chrono::milliseconds(entry.ttl_ms);
        store_->Put(entry.key, entry.value, entry.index, ttl);
    } else if (entry.opType == OpType::Delete) {
        store_->Delete(entry.key, entry.index);
    }

    if (applyCallback_) applyCallback_(entry);
}

void RaftNode::MaybeSnapshot() {
    if (log_->Size() > config_.snapshotThreshold) {
        auto snap = store_->TakeSnapshot();
        uint64_t snapIndex = lastApplied_;
        uint64_t snapTerm  = log_->GetTerm(snapIndex);
        if (wal_) {
            wal_->WriteSnapshot(snapIndex, snapTerm, snap);
        }
        log_->SetSnapshot(snapIndex, snapTerm);
        LOG_INFOF("[{}] Took snapshot at index={} term={}", config_.nodeId, snapIndex, snapTerm);
    }
}

// ── RPC Handlers ─────────────────────────────────────────────────────────────

AppendEntriesResponse RaftNode::HandleAppendEntries(const AppendEntriesRequest& req) {
    AppendEntriesResponse resp;
    std::lock_guard<std::mutex> lock(mu_);
    resp.term = currentTerm_;

    if (req.term < currentTerm_) { resp.success = false; return resp; }
    if (req.term > currentTerm_) BecomeFollower(req.term, req.leaderId);

    ResetElectionTimer();
    leaderId_ = req.leaderId;

    // Check prevLogIndex / prevLogTerm
    if (req.prevLogIndex > 0) {
        if (req.prevLogIndex > log_->LastIndex()) {
            resp.success       = false;
            resp.conflictIndex = log_->LastIndex() + 1;
            resp.conflictTerm  = 0;
            return resp;
        }
        uint64_t prevTerm = log_->GetTerm(req.prevLogIndex);
        if (prevTerm != req.prevLogTerm) {
            resp.success      = false;
            resp.conflictTerm = prevTerm;
            // Find first index with conflictTerm
            uint64_t ci = req.prevLogIndex;
            while (ci > log_->SnapshotIndex() && log_->GetTerm(ci - 1) == prevTerm) --ci;
            resp.conflictIndex = ci;
            return resp;
        }
    }

    // Append new entries
    if (!req.entries.empty()) {
        log_->AppendAll(req.entries);
    }

    // Advance commit index
    if (req.leaderCommit > commitIndex_) {
        commitIndex_.store(std::min(req.leaderCommit, log_->LastIndex()));
        applyCv_.notify_all();
    }

    resp.success = true;
    return resp;
}

RequestVoteResponse RaftNode::HandleRequestVote(const RequestVoteRequest& req) {
    RequestVoteResponse resp;
    std::lock_guard<std::mutex> lock(mu_);
    resp.term = currentTerm_;

    if (req.term < currentTerm_) { resp.voteGranted = false; return resp; }
    if (req.term > currentTerm_) BecomeFollower(req.term);

    bool alreadyVoted = !votedFor_.empty() && votedFor_ != req.candidateId;
    bool logUpToDate  = IsLogUpToDate(req.lastLogIndex, req.lastLogTerm);

    if (!alreadyVoted && logUpToDate) {
        votedFor_ = req.candidateId;
        ResetElectionTimer();
        PersistHardState();
        resp.voteGranted = true;
        LOG_INFOF("[{}] Granted vote to {} for term {}", config_.nodeId, req.candidateId, req.term);
    } else {
        resp.voteGranted = false;
    }

    return resp;
}

bool RaftNode::IsLogUpToDate(uint64_t lastLogIndex, uint64_t lastLogTerm) const {
    uint64_t myLastTerm  = log_->LastTerm();
    uint64_t myLastIndex = log_->LastIndex();
    if (lastLogTerm != myLastTerm) return lastLogTerm > myLastTerm;
    return lastLogIndex >= myLastIndex;
}

// ── Propose ───────────────────────────────────────────────────────────────────

ProposeResult RaftNode::Propose(const std::string& key, const std::string& value,
                                 OpType op, int64_t ttl_ms,
                                 std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    if (role_ != RaftRole::Leader) {
        return {false, "not leader (leader=" + leaderId_ + ")", 0};
    }

    uint64_t index = log_->LastIndex() + 1;
    LogEntry entry;
    entry.term    = currentTerm_;
    entry.index   = index;
    entry.opType  = op;
    entry.key     = key;
    entry.value   = value;
    entry.ttl_ms  = ttl_ms;
    entry.command = entry.Serialize();

    log_->Append(entry);

    // Wait for commit
    auto deadline = std::chrono::steady_clock::now() + timeout;
    bool committed = proposeCv_.wait_until(lock, deadline, [this, index]{
        return lastApplied_ >= index || role_ != RaftRole::Leader;
    });

    if (!committed) return {false, "timeout waiting for commit", index};
    if (role_ != RaftRole::Leader) return {false, "lost leadership during proposal", index};
    return {true, "", index};
}

// ── Accessors ─────────────────────────────────────────────────────────────────

bool        RaftNode::IsLeader()       const { return role_ == RaftRole::Leader; }
RaftRole    RaftNode::GetRole()        const { return role_.load(); }
uint64_t    RaftNode::GetCurrentTerm() const { return currentTerm_.load(); }
uint64_t    RaftNode::GetCommitIndex() const { return commitIndex_.load(); }
std::string RaftNode::GetLeaderId()    const {
    std::lock_guard<std::mutex> lock(mu_);
    return leaderId_;
}

} // namespace orcdb
