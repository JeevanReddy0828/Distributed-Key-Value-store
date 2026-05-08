#include "TokenStore.hpp"

namespace orcdb {

void TokenStore::Revoke(const std::string& jti) {
    std::lock_guard<std::mutex> lock(mu_);
    revoked_.insert(jti);
}

bool TokenStore::IsRevoked(const std::string& jti) const {
    std::lock_guard<std::mutex> lock(mu_);
    return revoked_.count(jti) > 0;
}

size_t TokenStore::RevokedCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return revoked_.size();
}

} // namespace orcdb
