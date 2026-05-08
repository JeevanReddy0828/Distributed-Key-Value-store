#include "KVHandler.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <optional>

namespace orcdb {

using json = nlohmann::json;

KVHandler::KVHandler(std::shared_ptr<RaftNode> raft, std::shared_ptr<KVStore> store)
    : raft_(std::move(raft)), store_(std::move(store)) {}

void KVHandler::RegisterRoutes(Router& router) {
    using namespace std::placeholders;

    // Register more-specific patterns first so they match before greedy {key}
    router.AddRoute("POST",   "/v1/kv/{key}/cas", [this](auto& req, auto& auth){ return CompareAndSwap(req, auth); }, {"write"});
    router.AddRoute("GET",    "/v1/kv",           [this](auto& req, auto& auth){ return List(req, auth); },   {"read"});
    router.AddRoute("GET",    "/v1/kv/{key}",     [this](auto& req, auto& auth){ return Get(req, auth); },    {"read"});
    router.AddRoute("PUT",    "/v1/kv/{key}",     [this](auto& req, auto& auth){ return Put(req, auth); },    {"write"});
    router.AddRoute("DELETE", "/v1/kv/{key}",     [this](auto& req, auto& auth){ return Delete(req, auth); }, {"write"});
}

HttpResponse KVHandler::Get(HttpRequest& req, const AuthContext&) {
    std::string key = req.pathParams.at("key");

    // consistent=true → linearizable read via leader confirmation
    // For simplicity: always serve from local KV (eventual reads)
    auto entry = store_->Get(key);
    if (!entry) return HttpResponse::NotFound("key not found: " + key);

    json body = {
        {"key",     key},
        {"value",   entry->value},
        {"version", entry->version},
        {"raft_index", entry->raftIndex}
    };
    return HttpResponse::Ok(body.dump());
}

HttpResponse KVHandler::Put(HttpRequest& req, const AuthContext&) {
    std::string key = req.pathParams.at("key");
    if (key.empty()) return HttpResponse::BadRequest("key cannot be empty");

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return HttpResponse::BadRequest("invalid JSON body"); }

    if (!body.contains("value")) return HttpResponse::BadRequest("missing field: value");
    std::string value = body["value"].get<std::string>();

    std::optional<std::chrono::milliseconds> ttl;
    if (body.contains("ttl_ms") && !body["ttl_ms"].is_null()) {
        ttl = std::chrono::milliseconds(body["ttl_ms"].get<int64_t>());
    }

    if (!raft_->IsLeader()) return RedirectToLeader(req);

    int64_t ttl_ms = ttl ? ttl->count() : -1;
    auto result = raft_->Propose(key, value, OpType::Put, ttl_ms);
    if (!result.ok) {
        if (result.error.find("not leader") != std::string::npos)
            return RedirectToLeader(req);
        return HttpResponse::InternalError(result.error);
    }

    json resp = {{"key", key}, {"version", 1}, {"raft_index", result.index}};
    return HttpResponse::Created(resp.dump());
}

HttpResponse KVHandler::Delete(HttpRequest& req, const AuthContext&) {
    std::string key = req.pathParams.at("key");

    if (!raft_->IsLeader()) return RedirectToLeader(req);

    auto entry = store_->Get(key);
    if (!entry) return HttpResponse::NotFound("key not found: " + key);

    auto result = raft_->Propose(key, "", OpType::Delete);
    if (!result.ok) return HttpResponse::InternalError(result.error);

    return HttpResponse::NoContent();
}

HttpResponse KVHandler::List(HttpRequest& req, const AuthContext&) {
    std::string prefix = req.GetQueryParam("prefix").value_or("");
    std::optional<size_t> limit;
    auto limitStr = req.GetQueryParam("limit");
    if (limitStr) limit = std::stoull(*limitStr);

    auto entries = store_->Scan(prefix, limit);
    json arr = json::array();
    for (auto& [k, v] : entries) {
        arr.push_back({{"key", k}, {"value", v.value}, {"version", v.version}});
    }
    return HttpResponse::Ok(json{{"items", arr}, {"count", arr.size()}}.dump());
}

HttpResponse KVHandler::CompareAndSwap(HttpRequest& req, const AuthContext&) {
    std::string key = req.pathParams.at("key");

    json body;
    try { body = json::parse(req.body); }
    catch (...) { return HttpResponse::BadRequest("invalid JSON body"); }

    if (!body.contains("expected") || !body.contains("value"))
        return HttpResponse::BadRequest("missing expected or value field");

    std::string expected = body["expected"].get<std::string>();
    std::string newValue = body["value"].get<std::string>();

    if (!raft_->IsLeader()) return RedirectToLeader(req);

    bool ok = store_->CAS(key, expected, newValue, 0);
    if (!ok) return HttpResponse::Conflict("compare-and-swap failed: value mismatch");

    return HttpResponse::Ok(json{{"key", key}, {"updated", true}}.dump());
}

HttpResponse KVHandler::RedirectToLeader(const HttpRequest& req) const {
    std::string leaderId = raft_->GetLeaderId();
    if (leaderId.empty()) {
        return HttpResponse::ServiceUnavailable("no leader elected");
    }
    // Simple redirect hint; clients should retry against the leader
    return HttpResponse::ServiceUnavailable(
        "not the leader; redirect to: " + leaderId);
}

} // namespace orcdb
