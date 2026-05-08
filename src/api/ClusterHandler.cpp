#include "ClusterHandler.hpp"
#include "../raft/RaftState.hpp"
#include <nlohmann/json.hpp>

namespace orcdb {

using json = nlohmann::json;

ClusterHandler::ClusterHandler(std::shared_ptr<RaftNode> raft)
    : raft_(std::move(raft)) {}

void ClusterHandler::RegisterRoutes(Router& router) {
    router.AddRoute("GET", "/v1/cluster/status",
        [this](auto& req, auto& auth){ return Status(req, auth); }, {"read"});
    router.AddRoute("GET", "/v1/cluster/leader",
        [this](auto& req, auto& auth){ return Leader(req, auth); });
}

HttpResponse ClusterHandler::Status(HttpRequest&, const AuthContext&) {
    json body = {
        {"role",         RaftRoleToString(raft_->GetRole())},
        {"current_term", raft_->GetCurrentTerm()},
        {"commit_index", raft_->GetCommitIndex()},
        {"leader_id",    raft_->GetLeaderId()},
        {"is_leader",    raft_->IsLeader()}
    };
    return HttpResponse::Ok(body.dump());
}

HttpResponse ClusterHandler::Leader(HttpRequest&, const AuthContext&) {
    std::string leaderId = raft_->GetLeaderId();
    if (leaderId.empty()) {
        return HttpResponse::ServiceUnavailable("no leader elected");
    }
    json body = {{"leader_id", leaderId}, {"is_leader", raft_->IsLeader()}};
    return HttpResponse::Ok(body.dump());
}

} // namespace orcdb
