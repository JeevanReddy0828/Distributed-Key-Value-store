#pragma once
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <cstdint>

namespace orcdb {

// ── Lightweight Prometheus-compatible metric primitives ───────────────────────

class Counter {
public:
    explicit Counter(std::string name, std::string help)
        : name_(std::move(name)), help_(std::move(help)) {}
    void Inc(double v = 1.0) { value_ += static_cast<uint64_t>(v); }
    uint64_t Value() const { return value_.load(); }
    std::string Emit() const {
        std::ostringstream os;
        os << "# HELP " << name_ << " " << help_ << "\n"
           << "# TYPE " << name_ << " counter\n"
           << name_ << " " << value_.load() << "\n";
        return os.str();
    }
private:
    std::string         name_, help_;
    std::atomic<uint64_t> value_{0};
};

class Gauge {
public:
    explicit Gauge(std::string name, std::string help)
        : name_(std::move(name)), help_(std::move(help)) {}
    void Set(double v)  { value_.store(static_cast<int64_t>(v * 1000)); }
    void Inc(double v = 1.0) { value_.fetch_add(static_cast<int64_t>(v * 1000)); }
    void Dec(double v = 1.0) { value_.fetch_sub(static_cast<int64_t>(v * 1000)); }
    double Value() const { return value_.load() / 1000.0; }
    std::string Emit() const {
        std::ostringstream os;
        os << "# HELP " << name_ << " " << help_ << "\n"
           << "# TYPE " << name_ << " gauge\n"
           << name_ << " " << (value_.load() / 1000.0) << "\n";
        return os.str();
    }
private:
    std::string           name_, help_;
    std::atomic<int64_t>  value_{0};
};

// Simple histogram with configurable buckets
class Histogram {
public:
    Histogram(std::string name, std::string help, std::vector<double> buckets)
        : name_(std::move(name)), help_(std::move(help)), buckets_(std::move(buckets))
    {
        counts_.resize(buckets_.size() + 1, 0); // +1 for +Inf bucket
    }

    void Observe(double v) {
        std::lock_guard<std::mutex> lock(mu_);
        sum_ += v;
        ++count_;
        for (size_t i = 0; i < buckets_.size(); ++i) {
            if (v <= buckets_[i]) ++counts_[i];
        }
        ++counts_.back(); // +Inf always increments
    }

    std::string Emit() const {
        std::lock_guard<std::mutex> lock(mu_);
        std::ostringstream os;
        os << "# HELP " << name_ << " " << help_ << "\n"
           << "# TYPE " << name_ << " histogram\n";
        for (size_t i = 0; i < buckets_.size(); ++i) {
            os << name_ << "_bucket{le=\"" << buckets_[i] << "\"} " << counts_[i] << "\n";
        }
        os << name_ << "_bucket{le=\"+Inf\"} " << counts_.back() << "\n"
           << name_ << "_sum "   << sum_   << "\n"
           << name_ << "_count " << count_ << "\n";
        return os.str();
    }

private:
    std::string          name_, help_;
    std::vector<double>  buckets_;
    mutable std::mutex   mu_;
    std::vector<uint64_t> counts_;
    double               sum_{0};
    uint64_t             count_{0};
};

// ── OrcMetrics: named singleton registry ──────────────────────────────────────

class OrcMetrics {
public:
    static OrcMetrics& Instance();

    // Raft
    Counter&   RaftElectionsTotal();
    Gauge&     RaftCurrentTerm();
    Gauge&     RaftIsLeader();
    Counter&   RaftCommittedEntries();
    Histogram& RaftReplicationLatencyMs();

    // API
    Counter&   HttpRequestsTotal();
    Histogram& HttpRequestDurationMs();
    Gauge&     ActiveConnections();

    // Storage
    Counter& KVPuts();
    Counter& KVGets();
    Counter& KVDeletes();
    Gauge&   KVStoreSize();
    Counter& WalBytesWritten();

    // Serialise all metrics to Prometheus text format
    std::string Collect() const;

private:
    OrcMetrics();

    Counter   raftElectionsTotal_{"orcdb_raft_elections_total", "Total Raft elections started"};
    Gauge     raftCurrentTerm_{"orcdb_raft_current_term", "Current Raft term"};
    Gauge     raftIsLeader_{"orcdb_raft_is_leader", "1 if this node is the Raft leader"};
    Counter   raftCommittedEntries_{"orcdb_raft_committed_entries_total", "Total committed log entries"};
    Histogram raftReplicationLatency_{"orcdb_raft_replication_latency_ms",
                                       "AppendEntries round-trip latency in ms",
                                       {1, 5, 10, 25, 50, 100, 250, 500}};

    Counter   httpRequestsTotal_{"orcdb_http_requests_total", "Total HTTP requests"};
    Histogram httpRequestDuration_{"orcdb_http_request_duration_ms",
                                    "HTTP request duration in ms",
                                    {1, 5, 10, 25, 50, 100, 250, 500, 1000}};
    Gauge     activeConnections_{"orcdb_active_connections", "Active TCP connections"};

    Counter   kvPuts_{"orcdb_kv_puts_total", "Total KV put operations"};
    Counter   kvGets_{"orcdb_kv_gets_total", "Total KV get operations"};
    Counter   kvDeletes_{"orcdb_kv_deletes_total", "Total KV delete operations"};
    Gauge     kvStoreSize_{"orcdb_kv_store_size", "Number of keys in the KV store"};
    Counter   walBytesWritten_{"orcdb_wal_bytes_written_total", "Bytes written to WAL"};
};

} // namespace orcdb
