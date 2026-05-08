#pragma once
#include "../api/Router.hpp"

namespace orcdb {

class MetricsHandler {
public:
    MetricsHandler() = default;
    void RegisterRoutes(Router& router);

private:
    HttpResponse Metrics(HttpRequest& req, const AuthContext& auth);
};

} // namespace orcdb
