#include <gtest/gtest.h>
#include "network/HttpParser.hpp"

using namespace orcdb;

TEST(HttpParserTest, SimpleGetRequest) {
    HttpParser p;
    std::string raw = "GET /v1/kv/foo HTTP/1.1\r\nHost: localhost\r\n\r\n";
    auto result = p.Feed(raw);
    EXPECT_EQ(result, ParseResult::Complete);
    ASSERT_TRUE(p.HasCompleteRequest());
    auto req = p.TakeRequest();
    EXPECT_EQ(req.method,  "GET");
    EXPECT_EQ(req.path,    "/v1/kv/foo");
    EXPECT_EQ(req.version, "HTTP/1.1");
}

TEST(HttpParserTest, PutRequestWithBody) {
    HttpParser p;
    std::string body = R"({"value":"hello"})";
    std::string raw  = "PUT /v1/kv/mykey HTTP/1.1\r\n"
                       "Content-Length: " + std::to_string(body.size()) + "\r\n"
                       "Content-Type: application/json\r\n\r\n" + body;
    auto result = p.Feed(raw);
    EXPECT_EQ(result, ParseResult::Complete);
    auto req = p.TakeRequest();
    EXPECT_EQ(req.method, "PUT");
    EXPECT_EQ(req.body,   body);
    EXPECT_EQ(req.GetHeader("Content-Type"), "application/json");
}

TEST(HttpParserTest, QueryStringParsed) {
    HttpParser p;
    std::string raw = "GET /v1/kv?prefix=/svc/&limit=10 HTTP/1.1\r\nHost: x\r\n\r\n";
    p.Feed(raw);
    auto req = p.TakeRequest();
    EXPECT_EQ(req.path, "/v1/kv");
    EXPECT_EQ(req.GetQueryParam("prefix").value_or(""), "/svc/");
    EXPECT_EQ(req.GetQueryParam("limit").value_or(""), "10");
}

TEST(HttpParserTest, IncrementalFeed) {
    HttpParser p;
    std::string raw = "GET /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n";
    // Feed byte by byte
    for (size_t i = 0; i < raw.size() - 1; ++i) {
        auto r = p.Feed(raw.data() + i, 1);
        EXPECT_NE(r, ParseResult::Error);
        EXPECT_FALSE(p.HasCompleteRequest());
    }
    auto r = p.Feed(raw.data() + raw.size() - 1, 1);
    EXPECT_EQ(r, ParseResult::Complete);
}

TEST(HttpParserTest, CaseInsensitiveHeader) {
    HttpParser p;
    std::string raw = "GET / HTTP/1.1\r\nAuthorization: Bearer token123\r\n\r\n";
    p.Feed(raw);
    auto req = p.TakeRequest();
    EXPECT_EQ(req.GetHeader("authorization"), "Bearer token123");
    EXPECT_EQ(req.GetHeader("AUTHORIZATION"), "Bearer token123");
}

TEST(HttpParserTest, KeepAlive_HTTP11_Default) {
    HttpParser p;
    p.Feed("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    auto req = p.TakeRequest();
    EXPECT_TRUE(req.IsKeepAlive());
}

TEST(HttpParserTest, KeepAlive_ConnectionClose) {
    HttpParser p;
    p.Feed("GET / HTTP/1.1\r\nConnection: close\r\n\r\n");
    auto req = p.TakeRequest();
    EXPECT_FALSE(req.IsKeepAlive());
}

TEST(HttpParserTest, EmptyBody_ZeroContentLength) {
    HttpParser p;
    p.Feed("DELETE /v1/kv/foo HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
    ASSERT_TRUE(p.HasCompleteRequest());
    auto req = p.TakeRequest();
    EXPECT_EQ(req.method, "DELETE");
    EXPECT_TRUE(req.body.empty());
}
