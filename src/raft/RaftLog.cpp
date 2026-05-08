#include "RaftLog.hpp"
#include "../storage/WriteAheadLog.hpp"
#include <stdexcept>

namespace orcdb {

RaftLog::RaftLog(std::shared_ptr<WriteAheadLog> wal) : wal_(std::move(wal)) {
    // Sentinel entry at index 0 / term 0
    LogEntry sentinel;
    sentinel.term  = 0;
    sentinel.index = 0;
    sentinel.opType = OpType::NoOp;
    entries_.push_back(sentinel);
}

void RaftLog::Append(const LogEntry& entry) {
    std::unique_lock<std::shared_mutex> lock(mu_);
    entries_.push_back(entry);
    if (wal_) {
        WriteAheadLog::RawLogEntry raw;
        raw.term    = entry.term;
        raw.index   = entry.index;
        raw.command = entry.Serialize();
        wal_->AppendLogEntry(raw);
    }
}

void RaftLog::AppendAll(const std::vector<LogEntry>& newEntries) {
    if (newEntries.empty()) return;
    std::unique_lock<std::shared_mutex> lock(mu_);

    for (auto& ne : newEntries) {
        size_t off = Offset(ne.index);
        if (off < entries_.size()) {
            if (entries_[off].term != ne.term) {
                // Conflicting entry: truncate from here
                entries_.resize(off);
                entries_.push_back(ne);
            }
            // Same term at same index: already have it
        } else {
            entries_.push_back(ne);
        }
        if (wal_) {
            WriteAheadLog::RawLogEntry raw;
            raw.term    = ne.term;
            raw.index   = ne.index;
            raw.command = ne.Serialize();
            wal_->AppendLogEntry(raw);
        }
    }
}

void RaftLog::TruncateFrom(uint64_t index) {
    std::unique_lock<std::shared_mutex> lock(mu_);
    size_t off = Offset(index);
    if (off < entries_.size()) {
        entries_.resize(off);
    }
}

LogEntry RaftLog::GetEntry(uint64_t index) const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    size_t off = Offset(index);
    if (off >= entries_.size()) {
        throw std::out_of_range("RaftLog::GetEntry: index " + std::to_string(index) + " out of range");
    }
    return entries_[off];
}

std::vector<LogEntry> RaftLog::GetEntries(uint64_t from, uint64_t to) const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    std::vector<LogEntry> result;
    for (uint64_t i = from; i < to && Offset(i) < entries_.size(); ++i) {
        result.push_back(entries_[Offset(i)]);
    }
    return result;
}

uint64_t RaftLog::LastIndex() const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    return entries_.back().index;
}

uint64_t RaftLog::LastTerm() const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    return entries_.back().term;
}

uint64_t RaftLog::GetTerm(uint64_t index) const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    if (index == snapshotIndex_) return snapshotTerm_;
    size_t off = Offset(index);
    if (off >= entries_.size()) return 0;
    return entries_[off].term;
}

void RaftLog::SetSnapshot(uint64_t index, uint64_t term) {
    std::unique_lock<std::shared_mutex> lock(mu_);
    // Discard log entries up to (and including) snapshot index
    size_t off = Offset(index);
    if (off < entries_.size()) {
        entries_.erase(entries_.begin(), entries_.begin() + off);
    } else {
        entries_.clear();
    }
    // Reset sentinel to snapshot position
    LogEntry sentinel;
    sentinel.term  = term;
    sentinel.index = index;
    sentinel.opType = OpType::NoOp;
    entries_.insert(entries_.begin(), sentinel);

    snapshotIndex_ = index;
    snapshotTerm_  = term;
}

uint64_t RaftLog::SnapshotIndex() const { return snapshotIndex_; }
uint64_t RaftLog::SnapshotTerm()  const { return snapshotTerm_; }
size_t   RaftLog::Size() const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    return entries_.size() - 1; // Exclude sentinel
}

} // namespace orcdb
