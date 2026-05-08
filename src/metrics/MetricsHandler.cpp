#include "MetricsHandler.hpp"
#include "OrcMetrics.hpp"

namespace orcdb {

void MetricsHandler::RegisterRoutes(Router& router) {
    router.AddRoute("GET", "/metrics",
        [this](auto& req, auto& auth){ return Metrics(req, auth); });
}

HttpResponse MetricsHandler::Metrics(HttpRequest&, const AuthContext&) {
    std::string body = OrcMetrics::Instance().Collect();
    return HttpResponse::Ok(body, "text/plain; version=0.0.4; charset=utf-8");
}

} // namespace orcdb
