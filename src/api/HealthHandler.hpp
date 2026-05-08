#pragma once
#include "Router.hpp"
#include "../raft/RaftNode.hpp"
#include <memory>

namespace orcdb {

class HealthHandler {
public:
    explicit HealthHandler(std::shared_ptr<RaftNode> raft);
    void RegisterRoutes(Router& router);

private:
    HttpResponse Liveness(HttpRequest& req, const AuthContext& auth);
    HttpResponse Readiness(HttpRequest& req, const AuthContext& auth);

    std::shared_ptr<RaftNode> raft_;
};

} // namespace orcdb
