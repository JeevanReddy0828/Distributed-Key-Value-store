#include "RaftPeer.hpp"
#include <stdexcept>
#include <cstring>

// POSIX sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace orcdb {

RaftPeer::RaftPeer(std::string peerId, std::string host, uint16_t port)
    : peerId_(std::move(peerId)), host_(std::move(host)), port_(port)
{
    Connect();
}

RaftPeer::~RaftPeer() {
    recvRunning_ = false;
    Disconnect();
    if (recvThread_.joinable()) recvThread_.join();
}

void RaftPeer::Connect() {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        return; // Will retry on next RPC
    }

    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return; }

    // TCP_NODELAY for low latency; no SO_RCVTIMEO — RecvLoop blocks until real data
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        ::close(fd);
        freeaddrinfo(res);
        return;
    }
    freeaddrinfo(res);

    sockfd_   = fd;
    connected_ = true;

    recvRunning_ = true;
    recvThread_ = std::thread([this]{ RecvLoop(); });
}

void RaftPeer::Disconnect() {
    connected_ = false;
    if (sockfd_ >= 0) {
        ::shutdown(sockfd_, SHUT_RDWR);
        ::close(sockfd_);
        sockfd_ = -1;
    }
    // Fail all pending RPCs
    std::lock_guard<std::mutex> lock(pendingMu_);
    for (auto& [seq, p] : pending_) {
        try { p.promise.set_exception(
            std::make_exception_ptr(std::runtime_error("peer disconnected"))); }
        catch (...) {}
    }
    pending_.clear();
}

void RaftPeer::RecvLoop() {
    std::vector<uint8_t> buf;
    buf.reserve(4096);
    uint8_t tmp[4096];

    while (recvRunning_ && connected_) {
        ssize_t n = ::recv(sockfd_, tmp, sizeof(tmp), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue; // transient
            Disconnect();
            break;
        }
        if (n == 0) { Disconnect(); break; } // peer closed
        buf.insert(buf.end(), tmp, tmp + n);

        size_t consumed = 0;
        while (true) {
            RpcFrame frame;
            size_t c = 0;
            if (!RpcFrame::Decode(buf.data() + consumed, buf.size() - consumed, frame, c)) break;
            consumed += c;

            // Resolve pending promise
            std::lock_guard<std::mutex> lock(pendingMu_);
            auto it = pending_.find(frame.seq);
            if (it != pending_.end()) {
                try { it->second.promise.set_value(frame.payload); }
                catch (...) {}
                pending_.erase(it);
            }
        }
        if (consumed > 0) buf.erase(buf.begin(), buf.begin() + consumed);
    }
}

bool RaftPeer::SendRaw(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(sendMu_);
    if (sockfd_ < 0) return false;
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(sockfd_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) { Disconnect(); return false; }
        sent += n;
    }
    return true;
}

uint32_t RaftPeer::NextSeq() { return seqGen_.fetch_add(1); }

std::future<AppendEntriesResponse> RaftPeer::SendAppendEntries(
    const AppendEntriesRequest& req,
    std::chrono::milliseconds timeout)
{
    auto seq = NextSeq();

    if (!connected_) Connect();
    if (!connected_) {
        // Peer unreachable — fail immediately rather than spawning a waiter thread.
        std::promise<AppendEntriesResponse> p;
        auto f = p.get_future();
        p.set_exception(std::make_exception_ptr(std::runtime_error("peer not connected")));
        return f;
    }

    std::promise<AppendEntriesResponse> outerPromise;
    auto future = outerPromise.get_future();

    // Register pending inner promise
    std::promise<std::string> innerPromise;
    auto innerFuture = innerPromise.get_future();
    {
        std::lock_guard<std::mutex> lock(pendingMu_);
        pending_[seq] = {std::move(innerPromise),
                         std::chrono::steady_clock::now() + timeout};
    }

    // Send request
    RpcFrame frame;
    frame.type    = RpcType::AppendEntriesReq;
    frame.seq     = seq;
    frame.payload = req.Serialize();
    bool sent     = SendRaw(frame.Encode());

    if (!sent) {
        std::lock_guard<std::mutex> lock(pendingMu_);
        pending_.erase(seq);
        outerPromise.set_exception(std::make_exception_ptr(
            std::runtime_error("send failed")));
        return future;
    }

    // Resolve outer promise on background thread with timeout
    std::thread([p = std::move(outerPromise),
                 f = std::move(innerFuture),
                 timeout]() mutable {
        if (f.wait_for(timeout) == std::future_status::ready) {
            try {
                auto payload = f.get();
                p.set_value(AppendEntriesResponse::Deserialize(payload));
            } catch (...) {
                p.set_exception(std::current_exception());
            }
        } else {
            p.set_exception(std::make_exception_ptr(
                std::runtime_error("RPC timeout")));
        }
    }).detach();

    return future;
}

std::future<RequestVoteResponse> RaftPeer::SendRequestVote(
    const RequestVoteRequest& req,
    std::chrono::milliseconds timeout)
{
    auto seq = NextSeq();

    if (!connected_) Connect();
    if (!connected_) {
        std::promise<RequestVoteResponse> p;
        auto f = p.get_future();
        p.set_exception(std::make_exception_ptr(std::runtime_error("peer not connected")));
        return f;
    }

    std::promise<RequestVoteResponse> outerPromise;
    auto future = outerPromise.get_future();

    std::promise<std::string> innerPromise;
    auto innerFuture = innerPromise.get_future();
    {
        std::lock_guard<std::mutex> lock(pendingMu_);
        pending_[seq] = {std::move(innerPromise),
                         std::chrono::steady_clock::now() + timeout};
    }

    RpcFrame frame;
    frame.type    = RpcType::RequestVoteReq;
    frame.seq     = seq;
    frame.payload = req.Serialize();
    bool sent     = SendRaw(frame.Encode());

    if (!sent) {
        std::lock_guard<std::mutex> lock(pendingMu_);
        pending_.erase(seq);
        outerPromise.set_exception(std::make_exception_ptr(
            std::runtime_error("send failed")));
        return future;
    }

    std::thread([p = std::move(outerPromise),
                 f = std::move(innerFuture),
                 timeout]() mutable {
        if (f.wait_for(timeout) == std::future_status::ready) {
            try {
                p.set_value(RequestVoteResponse::Deserialize(f.get()));
            } catch (...) {
                p.set_exception(std::current_exception());
            }
        } else {
            p.set_exception(std::make_exception_ptr(std::runtime_error("RPC timeout")));
        }
    }).detach();

    return future;
}

} // namespace orcdb
