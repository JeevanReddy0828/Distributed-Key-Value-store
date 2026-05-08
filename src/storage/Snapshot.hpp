#pragma once
#include <string>
#include <cstdint>

namespace orcdb {

struct Snapshot {
    uint64_t    index{0};
    uint64_t    term{0};
    std::string data;  // JSON-serialised KVStore state

    bool Empty() const { return index == 0; }

    std::string Serialise() const;
    static Snapshot Deserialise(const std::string& raw);
};

} // namespace orcdb
