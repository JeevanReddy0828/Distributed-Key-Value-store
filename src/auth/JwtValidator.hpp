#pragma once
#include "../api/Router.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace orcdb {

struct JwtClaims {
    std::string sub;
    std::string iss;
    std::vector<std::string> roles;
    int64_t exp{0};
    int64_t iat{0};
    std::string jti;
};

class JwtValidator {
public:
    struct Config {
        std::string algorithm{"HS256"};
        std::string secret;           // HS256
        std::string publicKeyPem;     // RS256
        std::string issuer{"orcdb"};
        int64_t     clockSkewSeconds{30};
    };

    explicit JwtValidator(Config config);
    ~JwtValidator();

    AuthContext Validate(const std::string& token) const;

    static std::optional<std::string> ExtractBearerToken(const std::string& authHeader);

    // Utility: generate a signed HS256 token (useful for dev/test)
    std::string GenerateHS256Token(const JwtClaims& claims) const;

private:
    bool VerifyHS256(const std::string& headerPayload, const std::string& sigBase64Url) const;
    bool VerifyRS256(const std::string& headerPayload, const std::string& sigBase64Url) const;
    bool ValidateClaims(const JwtClaims& claims) const;

    static std::string Base64UrlDecode(const std::string& s);
    static std::string Base64UrlEncode(const unsigned char* data, size_t len);
    static JwtClaims   ParsePayload(const std::string& payloadJson);

    Config config_;
    EVP_PKEY* rsaKey_{nullptr}; // for RS256
};

} // namespace orcdb
