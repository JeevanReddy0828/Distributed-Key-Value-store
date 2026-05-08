#pragma once
#include "raft/RaftPeer.hpp"
#include <gmock/gmock.h>
#include <future>

namespace orcdb {

class MockRaftPeer : public RaftPeer {
public:
    MockRaftPeer(std::string id, std::string host = "127.0.0.1", uint16_t port = 0)
        : id_(std::move(id)), host_(std::move(host)), port_(port) {}

    const std::string& Id() const { return id_; }
    bool IsConnected() const { return true; }

    MOCK_METHOD(std::future<AppendEntriesResponse>, SendAppendEntries,
                (const AppendEntriesRequest&, std::chrono::milliseconds), ());
    MOCK_METHOD(std::future<RequestVoteResponse>, SendRequestVote,
                (const RequestVoteRequest&, std::chrono::milliseconds), ());

    // Helpers to return ready futures
    static std::future<AppendEntriesResponse> SuccessResponse(uint64_t term = 1) {
        std::promise<AppendEntriesResponse> p;
        AppendEntriesResponse r;
        r.term    = term;
        r.success = true;
        p.set_value(r);
        return p.get_future();
    }

    static std::future<RequestVoteResponse> VoteGranted(uint64_t term = 1) {
        std::promise<RequestVoteResponse> p;
        RequestVoteResponse r;
        r.term        = term;
        r.voteGranted = true;
        p.set_value(r);
        return p.get_future();
    }

    static std::future<RequestVoteResponse> VoteDenied(uint64_t term = 1) {
        std::promise<RequestVoteResponse> p;
        RequestVoteResponse r;
        r.term        = term;
        r.voteGranted = false;
        p.set_value(r);
        return p.get_future();
    }

private:
    std::string id_, host_;
    uint16_t    port_;
};

} // namespace orcdb
