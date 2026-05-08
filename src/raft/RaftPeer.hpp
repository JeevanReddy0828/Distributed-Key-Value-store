#pragma once
#include "RaftRPC.hpp"
#include <string>
#include <future>
#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <vector>
#include <cstdint>

namespace orcdb {

// Manages a persistent TCP connection to one Raft peer.
// Provides async RPC with timeout and automatic reconnect.
class RaftPeer {
public:
    RaftPeer(std::string peerId, std::string host, uint16_t port);
    virtual ~RaftPeer();

    // Non-blocking RPCs: return future; resolves or times out
    virtual std::future<AppendEntriesResponse> SendAppendEntries(
        const AppendEntriesRequest& req,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(50));

    virtual std::future<RequestVoteResponse> SendRequestVote(
        const RequestVoteRequest& req,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(50));

    bool IsConnected() const { return connected_; }
    const std::string& Id() const { return peerId_; }

protected:
    RaftPeer() = default; // for mock subclass

private:
    void Connect();
    void Disconnect();
    void RecvLoop();
    bool SendRaw(const std::vector<uint8_t>& data);
    uint32_t NextSeq();

    std::string           peerId_;
    std::string           host_;
    uint16_t              port_{0};
    int                   sockfd_{-1};
    std::atomic<bool>     connected_{false};
    std::atomic<bool>     recvRunning_{false};
    std::thread           recvThread_;

    std::mutex            sendMu_;
    std::mutex            pendingMu_;
    std::atomic<uint32_t> seqGen_{0};

    struct Pending {
        std::promise<std::string> promise;
        std::chrono::steady_clock::time_point deadline;
    };
    std::unordered_map<uint32_t, Pending> pending_;

    std::vector<uint8_t> recvBuf_;
};

} // namespace orcdb
