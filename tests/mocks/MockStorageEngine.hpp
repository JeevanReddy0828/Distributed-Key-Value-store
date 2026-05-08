#pragma once
#include "storage/KVStore.hpp"
#include <gmock/gmock.h>

namespace orcdb {

class MockKVStore : public KVStore {
public:
    MOCK_METHOD(std::optional<KVEntry>, Get, (const std::string&), (const));
    MOCK_METHOD(bool, Put,
                (const std::string&, const std::string&, uint64_t,
                 std::optional<std::chrono::milliseconds>), ());
    MOCK_METHOD(bool, Delete, (const std::string&, uint64_t), ());
};

} // namespace orcdb
