#pragma once
#include "TcpConnection.hpp"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <cstdint>

namespace orcdb {

class TcpServer {
public:
    struct Config {
        std::string bindAddress{"0.0.0.0"};
        uint16_t    port{2379};
        int         backlog{1024};
        int         numIoThreads{4};
        std::chrono::milliseconds idleTimeout{30000};
    };

    TcpServer(Config config, RequestHandler handler);
    ~TcpServer();

    void Start();
    void Stop();

    size_t ActiveConnections() const;

private:
    void AcceptLoop();
    void IoLoop(int threadId, int epollFd);

    void HandleRead(TcpConnection& conn, int epollFd);
    void HandleWrite(TcpConnection& conn);
    void CloseConnection(TcpConnection& conn, int epollFd);

    static int SetNonBlocking(int fd);
    static void SetTcpNoDelay(int fd);

    Config          config_;
    RequestHandler  handler_;
    int             listenFd_{-1};
    std::atomic<bool> running_{false};

    std::thread              acceptThread_;
    std::vector<std::thread> ioThreads_;
    std::vector<int>         epollFds_;
    std::atomic<int>         nextThread_{0};

    // Shared connection map accessed by accept thread and IO threads.
    // shared_ptr allows IO threads to hold a reference without the global lock
    // while processing a request, preventing handler_ from serialising all threads.
    mutable std::mutex                                            connMu_;
    std::unordered_map<int, std::shared_ptr<TcpConnection>>      connections_;

    std::atomic<uint64_t> totalConnections_{0};
};

} // namespace orcdb
