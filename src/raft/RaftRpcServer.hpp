#pragma once
#include "RaftNode.hpp"
#include "RaftRPC.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <cstdint>
#include <vector>

namespace orcdb {

// Listens on the Raft port, reads RpcFrames, dispatches to RaftNode, sends responses.
class RaftRpcServer {
public:
    RaftRpcServer(uint16_t port, std::shared_ptr<RaftNode> node);
    ~RaftRpcServer();

    void Start();
    void Stop();

private:
    void AcceptLoop();
    void HandleConn(int fd);

    uint16_t                  port_;
    std::shared_ptr<RaftNode> node_;
    int                       listenFd_{-1};
    std::atomic<bool>         running_{false};
    std::thread               acceptThread_;
};

} // namespace orcdb
