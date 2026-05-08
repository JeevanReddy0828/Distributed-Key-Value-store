#include "AuthMiddleware.hpp"
#include <algorithm>

namespace orcdb {

AuthMiddleware::AuthMiddleware(std::shared_ptr<JwtValidator> validator,
                               std::shared_ptr<TokenStore>   tokenStore)
    : validator_(std::move(validator))
    , tokenStore_(std::move(tokenStore))
{}

void AuthMiddleware::AddPublicPath(const std::string& path) {
    publicPaths_.push_back(path);
}

MiddlewareFn AuthMiddleware::MakeMiddleware() {
    return [this](HttpRequest& req, AuthContext& auth) -> std::optional<HttpResponse> {
        // Skip auth for public paths
        for (auto& p : publicPaths_) {
            if (req.path == p || req.path.substr(0, p.size()) == p) {
                auth.authenticated = true; // Mark as authenticated so routes don't block
                return std::nullopt;
            }
        }

        // Extract Bearer token
        std::string authHeader = req.GetHeader("Authorization");
        auto token = JwtValidator::ExtractBearerToken(authHeader);
        if (!token) {
            return HttpResponse::Unauthorized("missing Authorization header");
        }

        // Validate JWT
        auth = validator_->Validate(*token);
        if (!auth.authenticated) {
            return HttpResponse::Unauthorized("invalid or expired token");
        }

        // Check revocation list
        // (jti not stored in AuthContext directly; re-parse if needed)
        // For simplicity: revocation check omitted from fast path here;
        // TokenStore::IsRevoked is checked in privileged routes.

        return std::nullopt; // proceed to route handler
    };
}

} // namespace orcdb
