#include <gtest/gtest.h>
#include "raft/RaftLog.hpp"

using namespace orcdb;

class RaftLogTest : public ::testing::Test {
protected:
    RaftLog log{nullptr}; // no WAL in unit tests
};

static LogEntry MakeEntry(uint64_t term, uint64_t index) {
    LogEntry e;
    e.term   = term;
    e.index  = index;
    e.opType = OpType::NoOp;
    return e;
}

TEST_F(RaftLogTest, InitialState_EmptyLog) {
    EXPECT_EQ(log.LastIndex(), 0u);
    EXPECT_EQ(log.LastTerm(),  0u);
    EXPECT_EQ(log.Size(),      0u);
}

TEST_F(RaftLogTest, Append_SingleEntry) {
    log.Append(MakeEntry(1, 1));
    EXPECT_EQ(log.LastIndex(), 1u);
    EXPECT_EQ(log.LastTerm(),  1u);
    EXPECT_EQ(log.Size(),      1u);
}

TEST_F(RaftLogTest, GetEntry_ReturnsCorrectEntry) {
    log.Append(MakeEntry(1, 1));
    log.Append(MakeEntry(1, 2));
    auto e = log.GetEntry(2);
    EXPECT_EQ(e.index, 2u);
    EXPECT_EQ(e.term,  1u);
}

TEST_F(RaftLogTest, GetEntry_OutOfRange_Throws) {
    EXPECT_THROW(log.GetEntry(99), std::out_of_range);
}

TEST_F(RaftLogTest, AppendAll_NoConflict) {
    log.Append(MakeEntry(1, 1));
    std::vector<LogEntry> entries = {MakeEntry(1, 2), MakeEntry(1, 3)};
    log.AppendAll(entries);
    EXPECT_EQ(log.LastIndex(), 3u);
}

TEST_F(RaftLogTest, AppendAll_TruncatesConflictingEntries) {
    log.Append(MakeEntry(1, 1));
    log.Append(MakeEntry(1, 2));
    log.Append(MakeEntry(1, 3));

    // Leader sends entry at index 2 with different term → should truncate 2 & 3
    std::vector<LogEntry> newEntries = {MakeEntry(2, 2), MakeEntry(2, 3)};
    log.AppendAll(newEntries);

    EXPECT_EQ(log.LastIndex(), 3u);
    EXPECT_EQ(log.GetTerm(2), 2u);
    EXPECT_EQ(log.GetTerm(3), 2u);
}

TEST_F(RaftLogTest, TruncateFrom_RemovesEntries) {
    log.Append(MakeEntry(1, 1));
    log.Append(MakeEntry(1, 2));
    log.Append(MakeEntry(1, 3));
    log.TruncateFrom(2);
    EXPECT_EQ(log.LastIndex(), 1u);
}

TEST_F(RaftLogTest, GetEntries_Range) {
    for (int i = 1; i <= 5; ++i) log.Append(MakeEntry(1, i));
    auto entries = log.GetEntries(2, 4); // [2, 4)
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].index, 2u);
    EXPECT_EQ(entries[1].index, 3u);
}

TEST_F(RaftLogTest, SetSnapshot_CompactsLog) {
    for (int i = 1; i <= 5; ++i) log.Append(MakeEntry(1, i));
    log.SetSnapshot(3, 1);
    EXPECT_EQ(log.SnapshotIndex(), 3u);
    EXPECT_EQ(log.SnapshotTerm(),  1u);
    EXPECT_EQ(log.LastIndex(), 5u);
    EXPECT_THROW(log.GetEntry(1), std::out_of_range);
    EXPECT_NO_THROW(log.GetEntry(4));
}

TEST_F(RaftLogTest, GetTerm_AtSnapshotIndex) {
    for (int i = 1; i <= 3; ++i) log.Append(MakeEntry(1, i));
    log.SetSnapshot(2, 1);
    EXPECT_EQ(log.GetTerm(2), 1u);
}
