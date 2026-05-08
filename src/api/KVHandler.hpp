#pragma once
#include "Router.hpp"
#include "../raft/RaftNode.hpp"
#include "../storage/KVStore.hpp"
#include <memory>

namespace orcdb {

class KVHandler {
public:
    KVHandler(std::shared_ptr<RaftNode> raft, std::shared_ptr<KVStore> store);

    void RegisterRoutes(Router& router);

private:
    HttpResponse Get(HttpRequest& req, const AuthContext& auth);
    HttpResponse Put(HttpRequest& req, const AuthContext& auth);
    HttpResponse Delete(HttpRequest& req, const AuthContext& auth);
    HttpResponse List(HttpRequest& req, const AuthContext& auth);
    HttpResponse CompareAndSwap(HttpRequest& req, const AuthContext& auth);

    HttpResponse RedirectToLeader(const HttpRequest& req) const;

    std::shared_ptr<RaftNode> raft_;
    std::shared_ptr<KVStore>  store_;
};

} // namespace orcdb
