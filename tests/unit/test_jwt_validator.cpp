#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "auth/JwtValidator.hpp"
#include <chrono>

using namespace orcdb;

class JwtValidatorTest : public ::testing::Test {
protected:
    JwtValidator::Config MakeConfig(const std::string& secret = "test-secret-key") {
        JwtValidator::Config cfg;
        cfg.algorithm = "HS256";
        cfg.secret    = secret;
        cfg.issuer    = "orcdb";
        return cfg;
    }

    JwtClaims MakeClaims(int64_t expOffsetSeconds = 3600) {
        JwtClaims c;
        c.sub   = "user1";
        c.iss   = "orcdb";
        c.roles = {"read", "write"};
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        c.iat = now;
        c.exp = now + expOffsetSeconds;
        c.jti = "test-jti-001";
        return c;
    }
};

TEST_F(JwtValidatorTest, ValidToken_ReturnsAuthenticated) {
    JwtValidator v(MakeConfig());
    auto token = v.GenerateHS256Token(MakeClaims());
    auto ctx   = v.Validate(token);
    EXPECT_TRUE(ctx.authenticated);
    EXPECT_EQ(ctx.subject, "user1");
    EXPECT_THAT(ctx.roles, ::testing::Contains("read"));
    EXPECT_THAT(ctx.roles, ::testing::Contains("write"));
}

TEST_F(JwtValidatorTest, ExpiredToken_ReturnsUnauthenticated) {
    JwtValidator v(MakeConfig());
    auto claims = MakeClaims(-100); // expired 100 seconds ago
    auto token  = v.GenerateHS256Token(claims);
    auto ctx    = v.Validate(token);
    EXPECT_FALSE(ctx.authenticated);
}

TEST_F(JwtValidatorTest, TamperedSignature_ReturnsUnauthenticated) {
    JwtValidator v(MakeConfig());
    auto token = v.GenerateHS256Token(MakeClaims());
    // Flip the last character of the signature
    token.back() = (token.back() == 'A') ? 'B' : 'A';
    auto ctx = v.Validate(token);
    EXPECT_FALSE(ctx.authenticated);
}

TEST_F(JwtValidatorTest, WrongSecret_ReturnsUnauthenticated) {
    JwtValidator signer(MakeConfig("secret-A"));
    JwtValidator verifier(MakeConfig("secret-B"));
    auto token = signer.GenerateHS256Token(MakeClaims());
    auto ctx   = verifier.Validate(token);
    EXPECT_FALSE(ctx.authenticated);
}

TEST_F(JwtValidatorTest, WrongIssuer_ReturnsUnauthenticated) {
    JwtValidator v(MakeConfig());
    auto claims = MakeClaims();
    claims.iss  = "wrong-issuer";
    auto token  = v.GenerateHS256Token(claims);
    auto ctx    = v.Validate(token);
    EXPECT_FALSE(ctx.authenticated);
}

TEST_F(JwtValidatorTest, ExtractBearerToken_ValidHeader) {
    auto token = JwtValidator::ExtractBearerToken("Bearer mytoken");
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(*token, "mytoken");
}

TEST_F(JwtValidatorTest, ExtractBearerToken_MissingBearer_ReturnsNullopt) {
    EXPECT_FALSE(JwtValidator::ExtractBearerToken("Basic abc").has_value());
    EXPECT_FALSE(JwtValidator::ExtractBearerToken("").has_value());
    EXPECT_FALSE(JwtValidator::ExtractBearerToken("mytoken").has_value());
}

TEST_F(JwtValidatorTest, InvalidTokenFormat_ReturnsUnauthenticated) {
    JwtValidator v(MakeConfig());
    EXPECT_FALSE(v.Validate("not.a.valid.jwt.format").authenticated);
    EXPECT_FALSE(v.Validate("justonepart").authenticated);
    EXPECT_FALSE(v.Validate("").authenticated);
}
