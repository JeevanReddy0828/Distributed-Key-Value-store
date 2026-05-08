#include "Snapshot.hpp"
#include <nlohmann/json.hpp>

namespace orcdb {

using json = nlohmann::json;

std::string Snapshot::Serialise() const {
    return json{{"index", index}, {"term", term}, {"data", data}}.dump();
}

Snapshot Snapshot::Deserialise(const std::string& raw) {
    auto j = json::parse(raw);
    Snapshot s;
    s.index = j["index"].get<uint64_t>();
    s.term  = j["term"].get<uint64_t>();
    s.data  = j["data"].get<std::string>();
    return s;
}

} // namespace orcdb
