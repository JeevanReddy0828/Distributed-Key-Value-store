#pragma once
#include <string>
#include <unordered_set>
#include <mutex>

namespace orcdb {

// Token revocation list (simple in-memory implementation).
// Production: back this with a distributed store (e.g., OrcDB itself).
class TokenStore {
public:
    void Revoke(const std::string& jti);
    bool IsRevoked(const std::string& jti) const;
    size_t RevokedCount() const;

private:
    mutable std::mutex            mu_;
    std::unordered_set<std::string> revoked_;
};

} // namespace orcdb
