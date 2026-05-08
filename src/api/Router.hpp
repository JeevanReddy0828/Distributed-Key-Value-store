#pragma once
#include "../network/HttpParser.hpp"
#include "../network/HttpResponse.hpp"
#include <string>
#include <vector>
#include <functional>
#include <regex>

namespace orcdb {

struct AuthContext {
    bool        authenticated{false};
    std::string subject;
    std::string rawToken;
    std::vector<std::string> roles;

    bool HasRole(const std::string& role) const;
};

using HandlerFn   = std::function<HttpResponse(HttpRequest&, const AuthContext&)>;
using MiddlewareFn = std::function<std::optional<HttpResponse>(HttpRequest&, AuthContext&)>;

struct Route {
    std::string              method;
    std::string              pattern;      // e.g. "/v1/kv/{key}"
    std::regex               regex;        // compiled from pattern
    std::vector<std::string> paramNames;   // extracted from {name} segments
    HandlerFn                handler;
    std::vector<std::string> requiredRoles;
};

class Router {
public:
    Router() = default;

    void AddRoute(const std::string& method, const std::string& pattern,
                  HandlerFn handler,
                  std::vector<std::string> requiredRoles = {});

    // Called by TcpServer for every parsed request
    HttpResponse Dispatch(HttpRequest& req, AuthContext& auth) const;

    // Auth middleware: runs before any route handler
    void SetAuthMiddleware(MiddlewareFn fn) { authMiddleware_ = std::move(fn); }

private:
    std::vector<Route> routes_;
    MiddlewareFn       authMiddleware_;

    static std::pair<std::regex, std::vector<std::string>>
        CompilePattern(const std::string& pattern);

    bool MatchRoute(const Route& route, const std::string& path,
                    std::unordered_map<std::string, std::string>& params) const;
};

} // namespace orcdb
