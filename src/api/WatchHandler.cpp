#include "WatchHandler.hpp"
#include <nlohmann/json.hpp>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace orcdb {

using json = nlohmann::json;

WatchHandler::WatchHandler(std::shared_ptr<KVStore> store) : store_(std::move(store)) {}

void WatchHandler::RegisterRoutes(Router& router) {
    router.AddRoute("GET", "/v1/watch/{key}",
        [this](auto& req, auto& auth){ return Watch(req, auth); }, {"read"});
}

HttpResponse WatchHandler::Watch(HttpRequest& req, const AuthContext&) {
    std::string key      = req.pathParams.at("key");
    bool        isPrefix = req.GetQueryParam("prefix").value_or("false") == "true";
    int64_t     timeoutMs = 30000;
    auto        ts = req.GetQueryParam("timeout_ms");
    if (ts) timeoutMs = std::stoll(*ts);

    // One-shot synchronisation: promise/future pair wired to KVStore watch
    std::mutex              mu;
    std::condition_variable cv;
    bool                    fired = false;
    std::optional<KVEntry>  changedEntry;
    std::string             changedKey;

    uint64_t watchId = store_->Watch(key, isPrefix,
        [&](const std::string& k, const std::optional<KVEntry>& e) {
            std::lock_guard<std::mutex> lock(mu);
            changedKey   = k;
            changedEntry = e;
            fired        = true;
            cv.notify_all();
        });

    bool ok = false;
    {
        std::unique_lock<std::mutex> lock(mu);
        ok = cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]{ return fired; });
    }
    store_->Unwatch(watchId);

    if (!ok) {
        // Timeout: return 204 No Content
        return HttpResponse::NoContent();
    }

    json body = {{"key", changedKey}};
    if (changedEntry) {
        body["value"]      = changedEntry->value;
        body["version"]    = changedEntry->version;
        body["raft_index"] = changedEntry->raftIndex;
        body["deleted"]    = false;
    } else {
        body["deleted"] = true;
    }
    return HttpResponse::Ok(body.dump());
}

} // namespace orcdb
