#include <gtest/gtest.h>
#include "auth/JwtValidator.hpp"
#include "auth/AuthMiddleware.hpp"
#include "auth/TokenStore.hpp"
#include "api/Router.hpp"
#include <chrono>

using namespace orcdb;

static HttpResponse SecretHandler(HttpRequest&, const AuthContext& auth) {
    return HttpResponse::Ok(R"({"secret":"data","user":")" + auth.subject + R"("})");
}

class AuthFlowTest : public ::testing::Test {
protected:
    std::shared_ptr<JwtValidator> validator;
    std::shared_ptr<TokenStore>   tokenStore;
    std::shared_ptr<AuthMiddleware> authMw;
    Router router;

    void SetUp() override {
        JwtValidator::Config cfg;
        cfg.algorithm = "HS256";
        cfg.secret    = "integration-test-secret";
        cfg.issuer    = "orcdb";

        validator  = std::make_shared<JwtValidator>(cfg);
        tokenStore = std::make_shared<TokenStore>();
        authMw     = std::make_shared<AuthMiddleware>(validator, tokenStore);

        router.SetAuthMiddleware(authMw->MakeMiddleware());
        router.AddRoute("GET",  "/healthz",   [](HttpRequest&, const AuthContext&){ return HttpResponse::Ok("{}"); });
        router.AddRoute("GET",  "/protected", SecretHandler, {"read"});
        router.AddRoute("POST", "/admin",     SecretHandler, {"admin"});
    }

    std::string MakeToken(const std::string& sub,
                           const std::vector<std::string>& roles,
                           int64_t expOffsetSecs = 3600) {
        JwtClaims c;
        c.sub   = sub;
        c.iss   = "orcdb";
        c.roles = roles;
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        c.iat = now;
        c.exp = now + expOffsetSecs;
        c.jti = "jti-" + sub;
        return validator->GenerateHS256Token(c);
    }

    HttpResponse Call(const std::string& method, const std::string& path,
                       const std::string& token = "") {
        HttpRequest req;
        req.method = method;
        req.path   = path;
        if (!token.empty()) req.headers["Authorization"] = "Bearer " + token;

        AuthContext auth;
        return router.Dispatch(req, auth);
    }
};

TEST_F(AuthFlowTest, PublicPath_NoTokenRequired) {
    auto resp = Call("GET", "/healthz");
    EXPECT_EQ(resp.statusCode, 200);
}

TEST_F(AuthFlowTest, ProtectedPath_WithValidToken_Returns200) {
    auto token = MakeToken("alice", {"read"});
    auto resp  = Call("GET", "/protected", token);
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("alice"), std::string::npos);
}

TEST_F(AuthFlowTest, ProtectedPath_NoToken_Returns401) {
    auto resp = Call("GET", "/protected");
    EXPECT_EQ(resp.statusCode, 401);
}

TEST_F(AuthFlowTest, ProtectedPath_ExpiredToken_Returns401) {
    auto token = MakeToken("bob", {"read"}, -100); // expired
    auto resp  = Call("GET", "/protected", token);
    EXPECT_EQ(resp.statusCode, 401);
}

TEST_F(AuthFlowTest, AdminRoute_WithReadRole_Returns403) {
    auto token = MakeToken("charlie", {"read"});
    auto resp  = Call("POST", "/admin", token);
    EXPECT_EQ(resp.statusCode, 403);
}

TEST_F(AuthFlowTest, AdminRoute_WithAdminRole_Returns200) {
    auto token = MakeToken("dave", {"admin"});
    auto resp  = Call("POST", "/admin", token);
    EXPECT_EQ(resp.statusCode, 200);
}

TEST_F(AuthFlowTest, InvalidBearerFormat_Returns401) {
    HttpRequest req;
    req.method = "GET";
    req.path   = "/protected";
    req.headers["Authorization"] = "Basic dXNlcjpwYXNz";
    AuthContext auth;
    auto resp = router.Dispatch(req, auth);
    EXPECT_EQ(resp.statusCode, 401);
}
