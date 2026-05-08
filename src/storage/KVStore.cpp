#include "KVStore.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace orcdb {

using json = nlohmann::json;

std::optional<KVEntry> KVStore::Get(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;

    // Check TTL
    if (it->second.expiresAt && *it->second.expiresAt < std::chrono::system_clock::now()) {
        return std::nullopt;
    }
    return it->second;
}

bool KVStore::Put(const std::string& key, const std::string& value,
                  uint64_t raftIndex,
                  std::optional<std::chrono::milliseconds> ttl) {
    KVEntry entry;
    entry.value     = value;
    entry.raftIndex = raftIndex;
    entry.updatedAt = std::chrono::system_clock::now();

    std::optional<KVEntry> result;
    {
        std::unique_lock<std::shared_mutex> lock(mu_);
        auto it = data_.find(key);
        if (it != data_.end()) {
            entry.version   = it->second.version + 1;
            entry.createdAt = it->second.createdAt;
        } else {
            entry.version   = 1;
            entry.createdAt = entry.updatedAt;
        }
        if (ttl) {
            entry.expiresAt = entry.updatedAt + *ttl;
        }
        data_[key] = entry;
        result = entry;
    }
    NotifyWatchers(key, result);
    return true;
}

bool KVStore::Delete(const std::string& key, uint64_t raftIndex) {
    {
        std::unique_lock<std::shared_mutex> lock(mu_);
        if (data_.erase(key) == 0) return false;
    }
    NotifyWatchers(key, std::nullopt);
    return true;
}

std::vector<std::pair<std::string, KVEntry>> KVStore::Scan(
    const std::string& prefix,
    std::optional<size_t> limit) const
{
    std::shared_lock<std::shared_mutex> lock(mu_);
    std::vector<std::pair<std::string, KVEntry>> results;
    auto now = std::chrono::system_clock::now();

    for (auto it = data_.lower_bound(prefix); it != data_.end(); ++it) {
        if (it->first.substr(0, prefix.size()) != prefix) break;
        if (it->second.expiresAt && *it->second.expiresAt < now) continue;
        results.emplace_back(it->first, it->second);
        if (limit && results.size() >= *limit) break;
    }
    return results;
}

bool KVStore::CAS(const std::string& key,
                  const std::string& expectedValue,
                  const std::string& newValue,
                  uint64_t raftIndex) {
    std::optional<KVEntry> result;
    {
        std::unique_lock<std::shared_mutex> lock(mu_);
        auto it = data_.find(key);
        if (it == data_.end()) return false;
        if (it->second.value != expectedValue) return false;

        it->second.value     = newValue;
        it->second.version   += 1;
        it->second.updatedAt = std::chrono::system_clock::now();
        it->second.raftIndex = raftIndex;
        result = it->second;
    }
    NotifyWatchers(key, result);
    return true;
}

uint64_t KVStore::Watch(const std::string& keyOrPrefix, bool isPrefix, WatchCallback cb) {
    uint64_t id = nextWatchId_.fetch_add(1);
    std::lock_guard<std::mutex> lock(watchMu_);
    watches_[id] = {keyOrPrefix, isPrefix, std::move(cb)};
    return id;
}

void KVStore::Unwatch(uint64_t watchId) {
    std::lock_guard<std::mutex> lock(watchMu_);
    watches_.erase(watchId);
}

void KVStore::NotifyWatchers(const std::string& key, const std::optional<KVEntry>& entry) {
    std::lock_guard<std::mutex> lock(watchMu_);
    for (auto& [id, tuple] : watches_) {
        auto& [pattern, isPrefix, cb] = tuple;
        bool matches = isPrefix
            ? key.substr(0, pattern.size()) == pattern
            : key == pattern;
        if (matches) {
            cb(key, entry);
        }
    }
}

std::string KVStore::TakeSnapshot() const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    auto now = std::chrono::system_clock::now();
    json j = json::array();
    for (auto& [k, v] : data_) {
        if (v.expiresAt && *v.expiresAt < now) continue;
        int64_t expiresMs = -1;
        if (v.expiresAt) {
            expiresMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                *v.expiresAt - std::chrono::system_clock::time_point{}).count();
        }
        j.push_back({{"key", k}, {"value", v.value}, {"version", v.version},
                     {"raft_index", v.raftIndex}, {"expires_ms", expiresMs}});
    }
    return j.dump();
}

void KVStore::RestoreSnapshot(const std::string& snapshotJson) {
    auto j = json::parse(snapshotJson);
    std::unique_lock<std::shared_mutex> lock(mu_);
    data_.clear();
    for (auto& item : j) {
        KVEntry e;
        e.value     = item["value"].get<std::string>();
        e.version   = item["version"].get<uint64_t>();
        e.raftIndex = item["raft_index"].get<uint64_t>();
        e.createdAt = std::chrono::system_clock::now();
        e.updatedAt = e.createdAt;
        int64_t expiresMs = item["expires_ms"].get<int64_t>();
        if (expiresMs >= 0) {
            e.expiresAt = std::chrono::system_clock::time_point{
                std::chrono::milliseconds{expiresMs}};
        }
        data_[item["key"].get<std::string>()] = std::move(e);
    }
}

size_t KVStore::Size() const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    return data_.size();
}

size_t KVStore::PurgeExpired() {
    auto now = std::chrono::system_clock::now();
    size_t purged = 0;
    std::vector<std::string> expiredKeys;

    {
        std::unique_lock<std::shared_mutex> lock(mu_);
        for (auto it = data_.begin(); it != data_.end(); ) {
            if (it->second.expiresAt && *it->second.expiresAt < now) {
                expiredKeys.push_back(it->first);
                it = data_.erase(it);
                ++purged;
            } else {
                ++it;
            }
        }
    }

    for (auto& key : expiredKeys) {
        NotifyWatchers(key, std::nullopt);
    }
    return purged;
}

} // namespace orcdb
