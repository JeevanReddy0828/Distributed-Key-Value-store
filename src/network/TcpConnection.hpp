#pragma once
#include "HttpParser.hpp"
#include "HttpResponse.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <mutex>
#include <functional>
#include <chrono>

namespace orcdb {

using RequestHandler = std::function<HttpResponse(HttpRequest&)>;

struct TcpConnection {
    int                    fd{-1};
    HttpParser             parser;
    std::vector<uint8_t>   writeBuf;
    std::mutex             writeMu;
    bool                   markedClose{false};
    std::chrono::steady_clock::time_point lastActivity;
    RequestHandler         handler;

    TcpConnection() : lastActivity(std::chrono::steady_clock::now()) {}

    void QueueWrite(const std::string& data) {
        std::lock_guard<std::mutex> lock(writeMu);
        writeBuf.insert(writeBuf.end(), data.begin(), data.end());
    }

    bool HasPendingWrite() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(writeMu));
        return !writeBuf.empty();
    }
};

} // namespace orcdb
