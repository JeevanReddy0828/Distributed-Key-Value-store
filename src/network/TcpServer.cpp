#include "TcpServer.hpp"
#include "../util/Logger.hpp"
#include <cstring>
#include <stdexcept>
#include <algorithm>

// Linux-specific I/O
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace orcdb {

static constexpr int MAX_EVENTS = 256;
static constexpr size_t READ_BUFSIZE = 65536;

TcpServer::TcpServer(Config config, RequestHandler handler)
    : config_(std::move(config)), handler_(std::move(handler))
{}

TcpServer::~TcpServer() { Stop(); }

int TcpServer::SetNonBlocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void TcpServer::SetTcpNoDelay(int fd) {
    int yes = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
}

void TcpServer::Start() {
    // Create listening socket
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) throw std::runtime_error("socket() failed");

    int yes = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));

    SetNonBlocking(listenFd_);

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(config_.port);
    ::inet_pton(AF_INET, config_.bindAddress.c_str(), &addr.sin_addr);

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error("bind() failed on port " + std::to_string(config_.port));
    }
    if (::listen(listenFd_, config_.backlog) < 0) {
        throw std::runtime_error("listen() failed");
    }

    running_ = true;

    // Create one epoll fd per IO thread
    for (int i = 0; i < config_.numIoThreads; ++i) {
        int efd = ::epoll_create1(0);
        if (efd < 0) throw std::runtime_error("epoll_create1() failed");
        epollFds_.push_back(efd);
        ioThreads_.emplace_back([this, i, efd]{ IoLoop(i, efd); });
    }

    acceptThread_ = std::thread([this]{ AcceptLoop(); });

    LOG_INFOF("TcpServer listening on {}:{} ({} IO threads)",
              config_.bindAddress, config_.port, config_.numIoThreads);
}

void TcpServer::Stop() {
    if (!running_.exchange(false)) return;
    if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
    if (acceptThread_.joinable()) acceptThread_.join();
    for (auto& t : ioThreads_) { if (t.joinable()) t.join(); }
    for (auto efd : epollFds_) { ::close(efd); }
    epollFds_.clear();
    ioThreads_.clear();
}

void TcpServer::AcceptLoop() {
    struct epoll_event ev{};
    // Register listenFd on first epoll fd to detect readability
    int monitorEpfd = ::epoll_create1(0);
    ev.events  = EPOLLIN;
    ev.data.fd = listenFd_;
    ::epoll_ctl(monitorEpfd, EPOLL_CTL_ADD, listenFd_, &ev);

    struct epoll_event events[8];
    while (running_) {
        int n = ::epoll_wait(monitorEpfd, events, 8, 100); // 100ms timeout to check running_
        for (int i = 0; i < n; ++i) {
            if (!(events[i].events & EPOLLIN)) continue;

            while (true) {
                struct sockaddr_in clientAddr{};
                socklen_t addrLen = sizeof(clientAddr);
                int clientFd = ::accept4(listenFd_,
                    reinterpret_cast<sockaddr*>(&clientAddr), &addrLen,
                    SOCK_NONBLOCK);
                if (clientFd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    break;
                }

                SetTcpNoDelay(clientFd);

                // Round-robin to IO threads
                int tid = nextThread_.fetch_add(1) % config_.numIoThreads;
                int epollFd = epollFds_[tid];

                auto conn = std::make_shared<TcpConnection>();
                conn->fd      = clientFd;
                conn->handler = handler_;
                {
                    std::lock_guard<std::mutex> lock(connMu_);
                    connections_[clientFd] = conn;
                }

                ev.events  = EPOLLIN | EPOLLET; // Edge-triggered
                ev.data.fd = clientFd;
                ::epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev);

                ++totalConnections_;
            }
        }
    }
    ::close(monitorEpfd);
}

void TcpServer::IoLoop(int threadId, int epollFd) {
    struct epoll_event events[MAX_EVENTS];

    while (running_) {
        int n = ::epoll_wait(epollFd, events, MAX_EVENTS, 100);
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            // Grab a shared_ptr under the lock, then release it.
            // This lets handler_ (which may block on Raft propose) run without
            // serialising all IO threads on connMu_.
            std::shared_ptr<TcpConnection> conn;
            {
                std::lock_guard<std::mutex> lock(connMu_);
                auto it = connections_.find(fd);
                if (it == connections_.end()) continue;
                conn = it->second;
            }

            bool shouldClose = false;

            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                shouldClose = true;
            } else {
                if (events[i].events & EPOLLIN) {
                    HandleRead(*conn, epollFd);
                    shouldClose |= conn->markedClose;
                }
                if (!shouldClose && (events[i].events & EPOLLOUT)) {
                    HandleWrite(*conn);
                    shouldClose |= (conn->markedClose && !conn->HasPendingWrite());
                }
            }

            if (shouldClose) {
                CloseConnection(*conn, epollFd);
                std::lock_guard<std::mutex> lock(connMu_);
                connections_.erase(fd);
            }
        }
    }
    (void)threadId;
}

void TcpServer::HandleRead(TcpConnection& conn, int epollFd) {
    uint8_t buf[READ_BUFSIZE];
    while (true) {
        ssize_t n = ::recv(conn.fd, buf, sizeof(buf), 0);
        if (n == 0) { conn.markedClose = true; return; }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            conn.markedClose = true;
            return;
        }

        conn.lastActivity = std::chrono::steady_clock::now();
        auto result = conn.parser.Feed(reinterpret_cast<char*>(buf), static_cast<size_t>(n));

        if (result == ParseResult::Complete) {
            auto req      = conn.parser.TakeRequest();
            auto response = handler_(req);
            bool keepAlive = req.IsKeepAlive() && !conn.markedClose;
            conn.QueueWrite(response.Serialise(keepAlive));
            if (!keepAlive) conn.markedClose = true;

            // Register for EPOLLOUT so we can flush the write buffer
            struct epoll_event ev{};
            ev.events  = EPOLLIN | EPOLLOUT | EPOLLET;
            ev.data.fd = conn.fd;
            ::epoll_ctl(epollFd, EPOLL_CTL_MOD, conn.fd, &ev);
        }
    }
}

void TcpServer::HandleWrite(TcpConnection& conn) {
    std::lock_guard<std::mutex> lock(conn.writeMu);
    while (!conn.writeBuf.empty()) {
        ssize_t n = ::send(conn.fd, conn.writeBuf.data(), conn.writeBuf.size(), MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            conn.markedClose = true;
            return;
        }
        conn.writeBuf.erase(conn.writeBuf.begin(), conn.writeBuf.begin() + n);
    }
}

void TcpServer::CloseConnection(TcpConnection& conn, int epollFd) {
    ::epoll_ctl(epollFd, EPOLL_CTL_DEL, conn.fd, nullptr);
    ::close(conn.fd);
}

size_t TcpServer::ActiveConnections() const {
    std::lock_guard<std::mutex> lock(connMu_);
    return connections_.size();
}

} // namespace orcdb
