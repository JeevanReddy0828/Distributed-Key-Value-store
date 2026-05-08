#include "OrcMetrics.hpp"

namespace orcdb {

OrcMetrics& OrcMetrics::Instance() {
    static OrcMetrics instance;
    return instance;
}

OrcMetrics::OrcMetrics() = default;

Counter&   OrcMetrics::RaftElectionsTotal()        { return raftElectionsTotal_; }
Gauge&     OrcMetrics::RaftCurrentTerm()            { return raftCurrentTerm_; }
Gauge&     OrcMetrics::RaftIsLeader()               { return raftIsLeader_; }
Counter&   OrcMetrics::RaftCommittedEntries()       { return raftCommittedEntries_; }
Histogram& OrcMetrics::RaftReplicationLatencyMs()   { return raftReplicationLatency_; }

Counter&   OrcMetrics::HttpRequestsTotal()          { return httpRequestsTotal_; }
Histogram& OrcMetrics::HttpRequestDurationMs()      { return httpRequestDuration_; }
Gauge&     OrcMetrics::ActiveConnections()          { return activeConnections_; }

Counter&   OrcMetrics::KVPuts()                     { return kvPuts_; }
Counter&   OrcMetrics::KVGets()                     { return kvGets_; }
Counter&   OrcMetrics::KVDeletes()                  { return kvDeletes_; }
Gauge&     OrcMetrics::KVStoreSize()                { return kvStoreSize_; }
Counter&   OrcMetrics::WalBytesWritten()            { return walBytesWritten_; }

std::string OrcMetrics::Collect() const {
    return raftElectionsTotal_.Emit()
         + raftCurrentTerm_.Emit()
         + raftIsLeader_.Emit()
         + raftCommittedEntries_.Emit()
         + raftReplicationLatency_.Emit()
         + httpRequestsTotal_.Emit()
         + httpRequestDuration_.Emit()
         + activeConnections_.Emit()
         + kvPuts_.Emit()
         + kvGets_.Emit()
         + kvDeletes_.Emit()
         + kvStoreSize_.Emit()
         + walBytesWritten_.Emit();
}

} // namespace orcdb
