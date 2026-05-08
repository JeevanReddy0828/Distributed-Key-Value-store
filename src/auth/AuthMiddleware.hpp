#pragma once
#include "JwtValidator.hpp"
#include "TokenStore.hpp"
#include "../api/Router.hpp"
#include <memory>
#include <vector>
#include <string>

namespace orcdb {

class AuthMiddleware {
public:
    AuthMiddleware(std::shared_ptr<JwtValidator> validator,
                   std::shared_ptr<TokenStore>   tokenStore);

    // Returns MiddlewareFn suitable for Router::SetAuthMiddleware
    MiddlewareFn MakeMiddleware();

    void AddPublicPath(const std::string& path);

private:
    std::shared_ptr<JwtValidator> validator_;
    std::shared_ptr<TokenStore>   tokenStore_;
    std::vector<std::string>      publicPaths_{"/healthz", "/readyz", "/metrics"};
};

} // namespace orcdb
