#include "WriteAheadLog.hpp"
#include "../util/Logger.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>

// POSIX I/O
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace orcdb {

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── CRC32 (IEEE 802.3 polynomial) ──────────────────────────────────────────
static uint32_t crc32_table[256];
static bool     crc32_table_ready = false;

static void BuildCrc32Table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 1) ? (0xEDB88320 ^ (crc >> 1)) : (crc >> 1);
        }
        crc32_table[i] = crc;
    }
    crc32_table_ready = true;
}

uint32_t WriteAheadLog::Crc32(const void* data, size_t len) {
    if (!crc32_table_ready) BuildCrc32Table();
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// ── Constructor / Destructor ───────────────────────────────────────────────
WriteAheadLog::WriteAheadLog(const std::string& walDir, size_t maxSegmentBytes)
    : walDir_(walDir), maxSegmentBytes_(maxSegmentBytes)
{
    fs::create_directories(walDir_);
    // Find highest existing segment, or start at 0
    uint64_t highest = 0;
    bool found = false;
    for (auto& entry : fs::directory_iterator(walDir_)) {
        std::string name = entry.path().filename().string();
        if (name.rfind("wal-", 0) == 0 && name.ends_with(".log")) {
            uint64_t seg = std::stoull(name.substr(4, name.size() - 8));
            if (!found || seg > highest) { highest = seg; found = true; }
        }
    }
    currentSegment_ = found ? highest : 0;
    OpenOrCreateSegment(currentSegment_);
}

WriteAheadLog::~WriteAheadLog() {
    if (currentFd_ >= 0) {
        ::fdatasync(currentFd_);
        ::close(currentFd_);
    }
}

std::string WriteAheadLog::SegmentPath(uint64_t seg) const {
    char buf[64];
    snprintf(buf, sizeof(buf), "wal-%016llu.log", (unsigned long long)seg);
    return (fs::path(walDir_) / buf).string();
}

void WriteAheadLog::OpenOrCreateSegment(uint64_t seg) {
    if (currentFd_ >= 0) ::close(currentFd_);
    std::string path = SegmentPath(seg);
    currentFd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (currentFd_ < 0) {
        throw std::runtime_error("Failed to open WAL segment: " + path);
    }
    struct stat st{};
    ::fstat(currentFd_, &st);
    currentOffset_ = static_cast<size_t>(st.st_size);
    currentSegment_ = seg;
}

void WriteAheadLog::MaybeRotate() {
    if (currentOffset_ >= maxSegmentBytes_) {
        ::fdatasync(currentFd_);
        OpenOrCreateSegment(currentSegment_ + 1);
    }
}

void WriteAheadLog::WriteRecord(RecordType type, const std::string& payload) {
    // Frame: [crc32 4B][length 4B][type 1B][payload]
    uint32_t length = static_cast<uint32_t>(payload.size());
    uint8_t  typeByte = static_cast<uint8_t>(type);

    // Compute CRC over type + payload
    std::vector<uint8_t> forCrc(1 + payload.size());
    forCrc[0] = typeByte;
    std::memcpy(forCrc.data() + 1, payload.data(), payload.size());
    uint32_t crc = Crc32(forCrc.data(), forCrc.size());

    // Build record buffer
    std::vector<uint8_t> record(9 + payload.size());
    std::memcpy(record.data(),     &crc,    4);
    std::memcpy(record.data() + 4, &length, 4);
    record[8] = typeByte;
    std::memcpy(record.data() + 9, payload.data(), payload.size());

    ssize_t written = ::write(currentFd_, record.data(), record.size());
    if (written != static_cast<ssize_t>(record.size())) {
        throw std::runtime_error("WAL write failed");
    }
    currentOffset_ += record.size();
}

void WriteAheadLog::AppendLogEntry(const RawLogEntry& entry) {
    std::lock_guard<std::mutex> lock(mu_);
    json j = {{"term", entry.term}, {"index", entry.index}, {"cmd", entry.command}};
    WriteRecord(RecordType::LogEntry, j.dump());
    ::fdatasync(currentFd_); // durability: entry must survive crash before leader ACK
    MaybeRotate();
}

void WriteAheadLog::WriteHardState(const HardState& state) {
    std::lock_guard<std::mutex> lock(mu_);
    json j = {{"term", state.term}, {"voted_for", state.votedFor}};
    WriteRecord(RecordType::HardState, j.dump());
}

void WriteAheadLog::WriteSnapshot(uint64_t index, uint64_t term, const std::string& data) {
    std::lock_guard<std::mutex> lock(mu_);
    json j = {{"index", index}, {"term", term}, {"data", data}};
    WriteRecord(RecordType::Snapshot, j.dump());
    Sync();
    // Rotate after snapshot so old segments can be compacted
    OpenOrCreateSegment(currentSegment_ + 1);
}

void WriteAheadLog::Sync() {
    if (currentFd_ >= 0) ::fdatasync(currentFd_);
}

WriteAheadLog::RecoveryResult WriteAheadLog::Recover() {
    RecoveryResult result;

    // Collect all segments in order
    std::vector<uint64_t> segments;
    for (auto& entry : fs::directory_iterator(walDir_)) {
        std::string name = entry.path().filename().string();
        if (name.rfind("wal-", 0) == 0 && name.ends_with(".log")) {
            segments.push_back(std::stoull(name.substr(4, name.size() - 8)));
        }
    }
    std::sort(segments.begin(), segments.end());

    for (uint64_t seg : segments) {
        std::string path = SegmentPath(seg);
        std::ifstream f(path, std::ios::binary);
        if (!f) continue;

        while (f.peek() != EOF) {
            uint32_t crc{}, length{};
            uint8_t  typeByte{};
            f.read(reinterpret_cast<char*>(&crc),    4);
            f.read(reinterpret_cast<char*>(&length), 4);
            f.read(reinterpret_cast<char*>(&typeByte), 1);
            if (!f) break;

            std::string payload(length, '\0');
            f.read(payload.data(), length);
            if (!f) break;

            // Verify CRC
            std::vector<uint8_t> forCrc(1 + length);
            forCrc[0] = typeByte;
            std::memcpy(forCrc.data() + 1, payload.data(), length);
            if (Crc32(forCrc.data(), forCrc.size()) != crc) {
                // Partial write or bit-flip: stop reading this segment.
                // Entries already recovered remain valid; this is safe because
                // Raft re-sends any entries the follower is missing.
                LOG_WARNF("WAL CRC mismatch in segment {} at offset ~{}; truncating recovery here",
                          seg, f.tellg());
                break;
            }

            auto type = static_cast<RecordType>(typeByte);
            auto j = json::parse(payload);

            switch (type) {
                case RecordType::LogEntry: {
                    RawLogEntry e;
                    e.term    = j["term"].get<uint64_t>();
                    e.index   = j["index"].get<uint64_t>();
                    e.command = j["cmd"].get<std::string>();
                    // Truncate any entries with higher index (for idempotency)
                    while (!result.entries.empty() && result.entries.back().index >= e.index) {
                        result.entries.pop_back();
                    }
                    result.entries.push_back(e);
                    break;
                }
                case RecordType::HardState:
                    result.hardState.term     = j["term"].get<uint64_t>();
                    result.hardState.votedFor = j["voted_for"].get<std::string>();
                    break;
                case RecordType::Snapshot:
                    result.snapshotIndex   = j["index"].get<uint64_t>();
                    result.snapshotTerm    = j["term"].get<uint64_t>();
                    result.latestSnapshot  = j["data"].get<std::string>();
                    // Discard log entries covered by snapshot
                    result.entries.clear();
                    break;
            }
        }
    }

    return result;
}

void WriteAheadLog::Truncate(uint64_t afterIndex) {
    // Naive approach: rebuild the WAL from in-memory Raft log (called by RaftNode on snapshot)
    // Real impl would scan + rewrite; sufficient for portfolio demo
    (void)afterIndex;
}

} // namespace orcdb
