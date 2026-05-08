#pragma once
#include "Router.hpp"
#include "../raft/RaftNode.hpp"
#include <memory>

namespace orcdb {

class ClusterHandler {
public:
    explicit ClusterHandler(std::shared_ptr<RaftNode> raft);
    void RegisterRoutes(Router& router);

private:
    HttpResponse Status(HttpRequest& req, const AuthContext& auth);
    HttpResponse Leader(HttpRequest& req, const AuthContext& auth);

    std::shared_ptr<RaftNode> raft_;
};

} // namespace orcdb
