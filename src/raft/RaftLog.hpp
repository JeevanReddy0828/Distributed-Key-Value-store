#pragma once
#include "RaftRPC.hpp"
#include <vector>
#include <shared_mutex>
#include <cstdint>
#include <memory>

namespace orcdb {

class WriteAheadLog;

// In-memory replicated log backed by WAL for durability.
// Index is 1-based; index 0 is the dummy sentinel entry.
class RaftLog {
public:
    explicit RaftLog(std::shared_ptr<WriteAheadLog> wal);

    // Append a single entry (leader path)
    void Append(const LogEntry& entry);

    // Append entries from leader, truncating conflicting suffix first
    void AppendAll(const std::vector<LogEntry>& entries);

    // Truncate to [0, index) — removes index and everything after
    void TruncateFrom(uint64_t index);

    LogEntry                 GetEntry(uint64_t index) const;
    std::vector<LogEntry>    GetEntries(uint64_t from, uint64_t to) const; // [from, to)

    uint64_t LastIndex() const;
    uint64_t LastTerm()  const;
    uint64_t GetTerm(uint64_t index) const;

    // Snapshot support
    void     SetSnapshot(uint64_t index, uint64_t term);
    uint64_t SnapshotIndex() const;
    uint64_t SnapshotTerm()  const;

    size_t Size() const;

private:
    // entries_[0] is always a sentinel with index = snapshotIndex_, term = snapshotTerm_
    std::vector<LogEntry>          entries_;
    uint64_t                       snapshotIndex_{0};
    uint64_t                       snapshotTerm_{0};
    mutable std::shared_mutex      mu_;
    std::shared_ptr<WriteAheadLog> wal_;

    // Converts absolute log index to entries_ vector offset
    size_t Offset(uint64_t index) const { return static_cast<size_t>(index - snapshotIndex_); }
};

} // namespace orcdb
