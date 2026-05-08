#pragma once
#include "Router.hpp"
#include "../storage/KVStore.hpp"
#include <memory>

namespace orcdb {

// Long-poll watch: connection held open until key changes or timeout.
class WatchHandler {
public:
    explicit WatchHandler(std::shared_ptr<KVStore> store);
    void RegisterRoutes(Router& router);

private:
    HttpResponse Watch(HttpRequest& req, const AuthContext& auth);

    std::shared_ptr<KVStore> store_;
};

} // namespace orcdb
