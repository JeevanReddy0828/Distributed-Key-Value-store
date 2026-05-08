#include "HealthHandler.hpp"
#include <nlohmann/json.hpp>

namespace orcdb {

using json = nlohmann::json;

HealthHandler::HealthHandler(std::shared_ptr<RaftNode> raft)
    : raft_(std::move(raft)) {}

void HealthHandler::RegisterRoutes(Router& router) {
    router.AddRoute("GET", "/healthz",
        [this](auto& req, auto& auth){ return Liveness(req, auth); });
    router.AddRoute("GET", "/readyz",
        [this](auto& req, auto& auth){ return Readiness(req, auth); });
}

HttpResponse HealthHandler::Liveness(HttpRequest&, const AuthContext&) {
    return HttpResponse::Ok(json{{"status", "ok"}}.dump());
}

HttpResponse HealthHandler::Readiness(HttpRequest&, const AuthContext&) {
    // Ready if we have a leader (can serve reads)
    bool ready = !raft_->GetLeaderId().empty();
    if (!ready) {
        return HttpResponse::ServiceUnavailable("no leader elected");
    }
    json body = {
        {"status",    "ready"},
        {"is_leader", raft_->IsLeader()},
        {"leader_id", raft_->GetLeaderId()},
        {"term",      raft_->GetCurrentTerm()}
    };
    return HttpResponse::Ok(body.dump());
}

} // namespace orcdb
