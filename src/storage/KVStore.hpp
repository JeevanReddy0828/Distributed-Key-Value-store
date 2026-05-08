#pragma once
#include <string>
#include <map>
#include <vector>
#include <optional>
#include <functional>
#include <shared_mutex>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>

namespace orcdb {

struct KVEntry {
    std::string value;
    uint64_t    version{0};
    uint64_t    raftIndex{0};
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point updatedAt;
    std::optional<std::chrono::system_clock::time_point> expiresAt;
};

using WatchCallback = std::function<void(const std::string& key,
                                          const std::optional<KVEntry>& entry)>;

class KVStore {
public:
    KVStore() = default;

    std::optional<KVEntry> Get(const std::string& key) const;
    bool Put(const std::string& key, const std::string& value,
             uint64_t raftIndex,
             std::optional<std::chrono::milliseconds> ttl = std::nullopt);
    bool Delete(const std::string& key, uint64_t raftIndex);

    // Prefix scan for service discovery
    std::vector<std::pair<std::string, KVEntry>> Scan(
        const std::string& prefix,
        std::optional<size_t> limit = std::nullopt) const;

    // Compare-and-swap: only updates if current value == expected
    bool CAS(const std::string& key,
             const std::string& expectedValue,
             const std::string& newValue,
             uint64_t raftIndex);

    // Watch: returns watch ID; callback fires on every change to key or prefix
    uint64_t Watch(const std::string& keyOrPrefix, bool isPrefix, WatchCallback cb);
    void Unwatch(uint64_t watchId);

    // Snapshot for Raft log compaction
    std::string TakeSnapshot() const;
    void RestoreSnapshot(const std::string& snapshotJson);

    size_t Size() const;

    // Purge expired entries (call periodically)
    size_t PurgeExpired();

private:
    mutable std::shared_mutex          mu_;
    std::map<std::string, KVEntry>     data_;

    std::mutex                                                          watchMu_;
    std::atomic<uint64_t>                                              nextWatchId_{1};
    std::unordered_map<uint64_t, std::tuple<std::string,bool,WatchCallback>> watches_;

    void NotifyWatchers(const std::string& key, const std::optional<KVEntry>& entry);
};

} // namespace orcdb
