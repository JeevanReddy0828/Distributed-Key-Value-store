#include "Router.hpp"
#include <algorithm>

namespace orcdb {

bool AuthContext::HasRole(const std::string& role) const {
    return std::find(roles.begin(), roles.end(), role) != roles.end() ||
           std::find(roles.begin(), roles.end(), "admin") != roles.end();
}

std::pair<std::regex, std::vector<std::string>>
Router::CompilePattern(const std::string& pattern) {
    std::vector<std::string> names;
    std::string regexStr = "^";

    size_t i = 0;
    while (i < pattern.size()) {
        if (pattern[i] == '{') {
            size_t end = pattern.find('}', i);
            if (end == std::string::npos) {
                regexStr += std::regex_replace(pattern.substr(i), std::regex("[.+*?^${}()|\\[\\]\\\\]"), "\\$&");
                i = pattern.size();
            } else {
                std::string name = pattern.substr(i + 1, end - i - 1);
                names.push_back(name);
                regexStr += "(.+)";
                i = end + 1;
            }
        } else {
            // Escape regex metachar
            char c = pattern[i++];
            if (std::string(".+*?^$|[]()\\").find(c) != std::string::npos) {
                regexStr += '\\';
            }
            regexStr += c;
        }
    }
    regexStr += "$";
    return {std::regex(regexStr), names};
}

void Router::AddRoute(const std::string& method, const std::string& pattern,
                      HandlerFn handler, std::vector<std::string> requiredRoles)
{
    Route r;
    r.method        = method;
    r.pattern       = pattern;
    r.handler       = std::move(handler);
    r.requiredRoles = std::move(requiredRoles);
    auto [re, names] = CompilePattern(pattern);
    r.regex      = std::move(re);
    r.paramNames = std::move(names);
    routes_.push_back(std::move(r));
}

bool Router::MatchRoute(const Route& route, const std::string& path,
                        std::unordered_map<std::string, std::string>& params) const
{
    std::smatch m;
    if (!std::regex_match(path, m, route.regex)) return false;
    for (size_t i = 0; i < route.paramNames.size() && i + 1 < m.size(); ++i) {
        params[route.paramNames[i]] = m[i + 1].str();
    }
    return true;
}

HttpResponse Router::Dispatch(HttpRequest& req, AuthContext& auth) const {
    // Run auth middleware first
    if (authMiddleware_) {
        auto maybeResp = authMiddleware_(req, auth);
        if (maybeResp) return *maybeResp;
    }

    for (auto& route : routes_) {
        if (route.method != req.method && route.method != "*") continue;

        std::unordered_map<std::string, std::string> params;
        if (!MatchRoute(route, req.path, params)) continue;

        // Inject path params into request
        req.pathParams = params;

        // Check RBAC
        if (!route.requiredRoles.empty()) {
            bool authorized = false;
            for (auto& role : route.requiredRoles) {
                if (auth.HasRole(role)) { authorized = true; break; }
            }
            if (!authorized) {
                return HttpResponse::Forbidden("insufficient role");
            }
        }

        return route.handler(req, auth);
    }

    return HttpResponse::NotFound("no route for " + req.method + " " + req.path);
}

} // namespace orcdb
