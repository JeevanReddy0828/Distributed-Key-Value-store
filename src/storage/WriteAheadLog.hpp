#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <optional>

namespace orcdb {

// WAL record: [CRC32 4B][length 4B][type 1B][payload ...]
class WriteAheadLog {
public:
    enum class RecordType : uint8_t {
        LogEntry  = 0x01,
        HardState = 0x02,
        Snapshot  = 0x03,
    };

    struct HardState {
        uint64_t    term{0};
        std::string votedFor;
    };

    struct RawLogEntry {
        uint64_t    term;
        uint64_t    index;
        std::string command; // serialized JSON
    };

    struct RecoveryResult {
        std::vector<RawLogEntry> entries;
        HardState                hardState;
        std::optional<std::string> latestSnapshot;
        uint64_t                 snapshotIndex{0};
        uint64_t                 snapshotTerm{0};
    };

    explicit WriteAheadLog(const std::string& walDir, size_t maxSegmentBytes = 64 * 1024 * 1024);
    ~WriteAheadLog();

    void AppendLogEntry(const RawLogEntry& entry);
    void WriteHardState(const HardState& state);
    void WriteSnapshot(uint64_t index, uint64_t term, const std::string& snapshotData);

    RecoveryResult Recover();

    void Sync();
    void Truncate(uint64_t afterIndex);

private:
    void OpenOrCreateSegment(uint64_t seg);
    void MaybeRotate();
    void WriteRecord(RecordType type, const std::string& payload);
    static uint32_t Crc32(const void* data, size_t len);
    std::string SegmentPath(uint64_t seg) const;

    std::string walDir_;
    size_t      maxSegmentBytes_;
    int         currentFd_{-1};
    uint64_t    currentSegment_{0};
    size_t      currentOffset_{0};
    std::mutex  mu_;
};

} // namespace orcdb
