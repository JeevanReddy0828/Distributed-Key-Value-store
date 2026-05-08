#pragma once
#include <string>

namespace orcdb {

struct Role {
    static constexpr const char* Read  = "read";
    static constexpr const char* Write = "write";
    static constexpr const char* Admin = "admin";
};

} // namespace orcdb
