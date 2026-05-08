#pragma once
#include <string>

namespace orcdb {

enum class RaftRole { Follower, Candidate, Leader };

inline std::string RaftRoleToString(RaftRole r) {
    switch (r) {
        case RaftRole::Follower:  return "follower";
        case RaftRole::Candidate: return "candidate";
        case RaftRole::Leader:    return "leader";
    }
    return "unknown";
}

} // namespace orcdb
