#include "JwtValidator.hpp"
#include <nlohmann/json.hpp>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/rsa.h>
#include <cstring>
#include <stdexcept>
#include <chrono>

namespace orcdb {

using json = nlohmann::json;

// ── Base64URL ─────────────────────────────────────────────────────────────────

static const std::string B64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string JwtValidator::Base64UrlEncode(const unsigned char* data, size_t len) {
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (uint32_t)data[i] << 16;
        if (i + 1 < len) val |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) val |= (uint32_t)data[i + 2];

        result += B64_CHARS[(val >> 18) & 0x3F];
        result += B64_CHARS[(val >> 12) & 0x3F];
        result += (i + 1 < len) ? B64_CHARS[(val >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? B64_CHARS[val & 0x3F] : '=';
    }

    // Convert to base64url: + → -, / → _, strip =
    for (char& c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!result.empty() && result.back() == '=') result.pop_back();
    return result;
}

std::string JwtValidator::Base64UrlDecode(const std::string& input) {
    std::string s = input;
    for (char& c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (s.size() % 4 != 0) s += '=';

    std::string result;
    result.reserve(s.size() * 3 / 4);

    static const int decoding[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };

    for (size_t i = 0; i + 3 < s.size(); i += 4) {
        int v0 = decoding[(unsigned char)s[i]];
        int v1 = decoding[(unsigned char)s[i+1]];
        int v2 = decoding[(unsigned char)s[i+2]];
        int v3 = decoding[(unsigned char)s[i+3]];
        if (v0 < 0 || v1 < 0) break;
        result += (char)((v0 << 2) | (v1 >> 4));
        if (v2 >= 0) result += (char)(((v1 & 0xF) << 4) | (v2 >> 2));
        if (v3 >= 0) result += (char)(((v2 & 0x3) << 6) | v3);
    }
    return result;
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

JwtValidator::JwtValidator(Config config) : config_(std::move(config)) {
    if (config_.algorithm == "RS256" && !config_.publicKeyPem.empty()) {
        BIO* bio = BIO_new_mem_buf(config_.publicKeyPem.data(),
                                    (int)config_.publicKeyPem.size());
        rsaKey_ = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!rsaKey_) throw std::runtime_error("Failed to load RS256 public key");
    }
}

JwtValidator::~JwtValidator() {
    if (rsaKey_) EVP_PKEY_free(rsaKey_);
}

// ── Validate ─────────────────────────────────────────────────────────────────

AuthContext JwtValidator::Validate(const std::string& token) const {
    AuthContext ctx;
    ctx.rawToken = token;

    // Split into 3 parts
    size_t dot1 = token.find('.');
    if (dot1 == std::string::npos) return ctx;
    size_t dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return ctx;

    std::string headerPayload = token.substr(0, dot2);
    std::string sigPart       = token.substr(dot2 + 1);
    std::string payloadB64    = token.substr(dot1 + 1, dot2 - dot1 - 1);

    // Decode and parse payload
    std::string payloadJson = Base64UrlDecode(payloadB64);
    JwtClaims claims;
    try { claims = ParsePayload(payloadJson); }
    catch (...) { return ctx; }

    // Verify signature
    bool sigOk = false;
    if (config_.algorithm == "HS256") {
        sigOk = VerifyHS256(headerPayload, sigPart);
    } else if (config_.algorithm == "RS256") {
        sigOk = VerifyRS256(headerPayload, sigPart);
    }

    if (!sigOk) return ctx;
    if (!ValidateClaims(claims)) return ctx;

    ctx.authenticated = true;
    ctx.subject       = claims.sub;
    ctx.roles         = claims.roles;
    return ctx;
}

bool JwtValidator::VerifyHS256(const std::string& headerPayload,
                                const std::string& sigBase64Url) const {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digestLen = 0;

    HMAC(EVP_sha256(),
         config_.secret.data(), (int)config_.secret.size(),
         reinterpret_cast<const unsigned char*>(headerPayload.data()),
         headerPayload.size(),
         digest, &digestLen);

    std::string expected = Base64UrlEncode(digest, digestLen);
    // Constant-time compare
    if (expected.size() != sigBase64Url.size()) return false;
    volatile int diff = 0;
    for (size_t i = 0; i < expected.size(); ++i) diff |= (expected[i] ^ sigBase64Url[i]);
    return diff == 0;
}

bool JwtValidator::VerifyRS256(const std::string& headerPayload,
                                const std::string& sigBase64Url) const {
    if (!rsaKey_) return false;

    std::string sigBytes = Base64UrlDecode(sigBase64Url);

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, rsaKey_);
    EVP_DigestVerifyUpdate(ctx,
        reinterpret_cast<const unsigned char*>(headerPayload.data()),
        headerPayload.size());
    int ok = EVP_DigestVerifyFinal(ctx,
        reinterpret_cast<const unsigned char*>(sigBytes.data()),
        sigBytes.size());
    EVP_MD_CTX_free(ctx);
    return ok == 1;
}

bool JwtValidator::ValidateClaims(const JwtClaims& claims) const {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (claims.exp > 0 && now > claims.exp + config_.clockSkewSeconds) return false;
    if (!config_.issuer.empty() && claims.iss != config_.issuer) return false;
    return true;
}

JwtClaims JwtValidator::ParsePayload(const std::string& payloadJson) {
    auto j = json::parse(payloadJson);
    JwtClaims c;
    c.sub = j.value("sub", "");
    c.iss = j.value("iss", "");
    c.exp = j.value("exp", int64_t{0});
    c.iat = j.value("iat", int64_t{0});
    c.jti = j.value("jti", "");
    if (j.contains("roles") && j["roles"].is_array()) {
        for (auto& r : j["roles"]) c.roles.push_back(r.get<std::string>());
    }
    return c;
}

std::optional<std::string> JwtValidator::ExtractBearerToken(const std::string& authHeader) {
    if (authHeader.substr(0, 7) != "Bearer ") return std::nullopt;
    return authHeader.substr(7);
}

std::string JwtValidator::GenerateHS256Token(const JwtClaims& claims) const {
    // Header
    std::string headerJson = R"({"alg":"HS256","typ":"JWT"})";
    std::string header = Base64UrlEncode(
        reinterpret_cast<const unsigned char*>(headerJson.data()), headerJson.size());

    // Payload
    json j = {{"sub", claims.sub}, {"iss", claims.iss},
              {"exp", claims.exp}, {"iat", claims.iat},
              {"jti", claims.jti}, {"roles", claims.roles}};
    std::string payloadJson = j.dump();
    std::string payload = Base64UrlEncode(
        reinterpret_cast<const unsigned char*>(payloadJson.data()), payloadJson.size());

    std::string headerPayload = header + "." + payload;

    // Sign
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digestLen = 0;
    HMAC(EVP_sha256(),
         config_.secret.data(), (int)config_.secret.size(),
         reinterpret_cast<const unsigned char*>(headerPayload.data()),
         headerPayload.size(), digest, &digestLen);
    std::string sig = Base64UrlEncode(digest, digestLen);

    return headerPayload + "." + sig;
}

} // namespace orcdb
