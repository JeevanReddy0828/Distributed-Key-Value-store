#include "RaftRpcServer.hpp"
#include "../util/Logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <stdexcept>

namespace orcdb {

RaftRpcServer::RaftRpcServer(uint16_t port, std::shared_ptr<RaftNode> node)
    : port_(port), node_(std::move(node))
{}

RaftRpcServer::~RaftRpcServer() { Stop(); }

void RaftRpcServer::Start() {
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) throw std::runtime_error("RaftRpcServer: socket() failed");

    int yes = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("RaftRpcServer: bind() failed on port " + std::to_string(port_));
    if (::listen(listenFd_, 32) < 0)
        throw std::runtime_error("RaftRpcServer: listen() failed");

    running_ = true;
    acceptThread_ = std::thread([this]{ AcceptLoop(); });
    LOG_INFOF("Raft RPC server listening on port {}", port_);
}

void RaftRpcServer::Stop() {
    if (!running_.exchange(false)) return;
    if (listenFd_ >= 0) { ::shutdown(listenFd_, SHUT_RDWR); ::close(listenFd_); listenFd_ = -1; }
    if (acceptThread_.joinable()) acceptThread_.join();
}

void RaftRpcServer::AcceptLoop() {
    while (running_) {
        int fd = ::accept(listenFd_, nullptr, nullptr);
        if (fd < 0) break;
        int yes = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
        // Handle on a detached thread — at most 2 peers in a 3-node cluster
        std::thread([this, fd]{ HandleConn(fd); }).detach();
    }
}

void RaftRpcServer::HandleConn(int fd) {
    std::vector<uint8_t> buf;
    buf.reserve(4096);
    uint8_t tmp[4096];

    while (true) {
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.insert(buf.end(), tmp, tmp + n);

        size_t consumed = 0;
        while (true) {
            RpcFrame req;
            size_t c = 0;
            if (!RpcFrame::Decode(buf.data() + consumed, buf.size() - consumed, req, c)) break;
            consumed += c;

            RpcFrame resp;
            resp.seq = req.seq;

            if (req.type == RpcType::AppendEntriesReq) {
                auto r = node_->HandleAppendEntries(
                    AppendEntriesRequest::Deserialize(req.payload));
                resp.type    = RpcType::AppendEntriesResp;
                resp.payload = r.Serialize();
            } else if (req.type == RpcType::RequestVoteReq) {
                auto r = node_->HandleRequestVote(
                    RequestVoteRequest::Deserialize(req.payload));
                resp.type    = RpcType::RequestVoteResp;
                resp.payload = r.Serialize();
            } else {
                break;
            }

            auto encoded = resp.Encode();
            ::send(fd, encoded.data(), encoded.size(), MSG_NOSIGNAL);
        }
        if (consumed > 0) buf.erase(buf.begin(), buf.begin() + consumed);
    }
    ::close(fd);
}

} // namespace orcdb
