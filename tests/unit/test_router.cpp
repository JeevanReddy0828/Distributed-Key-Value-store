#include <gtest/gtest.h>
#include "api/Router.hpp"

using namespace orcdb;

static HttpResponse OkHandler(HttpRequest&, const AuthContext&) {
    return HttpResponse::Ok(R"({"ok":true})");
}

TEST(RouterTest, ExactPathMatch) {
    Router r;
    r.AddRoute("GET", "/healthz", OkHandler);

    HttpRequest req;
    req.method = "GET";
    req.path   = "/healthz";
    AuthContext auth; auth.authenticated = true;

    auto resp = r.Dispatch(req, auth);
    EXPECT_EQ(resp.statusCode, 200);
}

TEST(RouterTest, PathWithParam_ExtractsParam) {
    Router r;
    r.AddRoute("GET", "/v1/kv/{key}", [](HttpRequest& req, const AuthContext&) {
        return HttpResponse::Ok(req.pathParams.at("key"));
    });

    HttpRequest req;
    req.method = "GET";
    req.path   = "/v1/kv/mykey";
    AuthContext auth; auth.authenticated = true;

    auto resp = r.Dispatch(req, auth);
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_EQ(resp.body, "mykey");
}

TEST(RouterTest, UnknownRoute_Returns404) {
    Router r;
    HttpRequest req;
    req.method = "GET";
    req.path   = "/unknown";
    AuthContext auth; auth.authenticated = true;
    auto resp = r.Dispatch(req, auth);
    EXPECT_EQ(resp.statusCode, 404);
}

TEST(RouterTest, MethodMismatch_Returns404) {
    Router r;
    r.AddRoute("GET", "/foo", OkHandler);

    HttpRequest req;
    req.method = "POST";
    req.path   = "/foo";
    AuthContext auth; auth.authenticated = true;
    auto resp = r.Dispatch(req, auth);
    EXPECT_EQ(resp.statusCode, 404);
}

TEST(RouterTest, RBAC_InsufficientRole_Returns403) {
    Router r;
    r.AddRoute("PUT", "/v1/kv/{key}", OkHandler, {"write"});

    HttpRequest req;
    req.method = "PUT";
    req.path   = "/v1/kv/x";
    AuthContext auth;
    auth.authenticated = true;
    auth.roles = {"read"}; // only read, not write

    auto resp = r.Dispatch(req, auth);
    EXPECT_EQ(resp.statusCode, 403);
}

TEST(RouterTest, RBAC_AdminBypassesAllRoles) {
    Router r;
    r.AddRoute("DELETE", "/v1/kv/{key}", OkHandler, {"write"});

    HttpRequest req;
    req.method = "DELETE";
    req.path   = "/v1/kv/x";
    AuthContext auth;
    auth.authenticated = true;
    auth.roles = {"admin"}; // admin covers everything

    auto resp = r.Dispatch(req, auth);
    EXPECT_EQ(resp.statusCode, 200);
}

TEST(RouterTest, AuthMiddleware_BlocksUnauthenticated) {
    Router r;
    r.SetAuthMiddleware([](HttpRequest&, AuthContext&) -> std::optional<HttpResponse> {
        return HttpResponse::Unauthorized();
    });
    r.AddRoute("GET", "/protected", OkHandler, {"read"});

    HttpRequest req;
    req.method = "GET";
    req.path   = "/protected";
    AuthContext auth;

    auto resp = r.Dispatch(req, auth);
    EXPECT_EQ(resp.statusCode, 401);
}
