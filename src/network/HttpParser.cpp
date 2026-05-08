#include "HttpParser.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace orcdb {

std::string HttpRequest::GetHeader(const std::string& name) const {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto& [k, v] : headers) {
        std::string kl = k;
        std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
        if (kl == lower) return v;
    }
    return "";
}

std::optional<std::string> HttpRequest::GetQueryParam(const std::string& key) const {
    auto it = queryParams.find(key);
    if (it == queryParams.end()) return std::nullopt;
    return it->second;
}

bool HttpRequest::IsKeepAlive() const {
    auto conn = GetHeader("connection");
    std::transform(conn.begin(), conn.end(), conn.begin(), ::tolower);
    if (version == "HTTP/1.1") return conn != "close";
    return conn == "keep-alive";
}

// ── URL decode ────────────────────────────────────────────────────────────────
static std::string UrlDecode(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int val = 0;
            for (int j = 1; j <= 2; ++j) {
                char c = s[i + j];
                val <<= 4;
                if      (c >= '0' && c <= '9') val |= c - '0';
                else if (c >= 'a' && c <= 'f') val |= c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') val |= c - 'A' + 10;
            }
            result += static_cast<char>(val);
            i += 2;
        } else if (s[i] == '+') {
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

void HttpParser::ParseQueryString(const std::string& raw) {
    std::istringstream ss(raw);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        auto eq = pair.find('=');
        if (eq == std::string::npos) {
            current_.queryParams[UrlDecode(pair)] = "";
        } else {
            current_.queryParams[UrlDecode(pair.substr(0, eq))] = UrlDecode(pair.substr(eq + 1));
        }
    }
}

ParseResult HttpParser::Feed(const std::string& data) {
    return Feed(data.data(), data.size());
}

ParseResult HttpParser::Feed(const char* data, size_t len) {
    buffer_.append(data, len);
    return ParseRequestLine();
}

ParseResult HttpParser::ParseRequestLine() {
    if (state_ == State::Complete || state_ == State::Error) return ParseResult::Error;

    auto pos = buffer_.find("\r\n");
    if (pos == std::string::npos) return ParseResult::Incomplete;

    std::string line = buffer_.substr(0, pos);
    buffer_.erase(0, pos + 2);
    consumed_ += pos + 2;

    std::istringstream ss(line);
    std::string method, fullPath, version;
    ss >> method >> fullPath >> version;
    if (method.empty() || fullPath.empty()) {
        state_ = State::Error;
        return ParseResult::Error;
    }

    current_.method  = method;
    current_.version = version;

    // Split path and query string
    auto qpos = fullPath.find('?');
    if (qpos != std::string::npos) {
        current_.path = fullPath.substr(0, qpos);
        ParseQueryString(fullPath.substr(qpos + 1));
    } else {
        current_.path = fullPath;
    }

    state_ = State::Headers;
    return ParseHeaders();
}

ParseResult HttpParser::ParseHeaders() {
    while (true) {
        auto pos = buffer_.find("\r\n");
        if (pos == std::string::npos) return ParseResult::Incomplete;

        std::string line = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 2);
        consumed_ += pos + 2;

        if (line.empty()) {
            // Header section ended
            auto it = current_.headers.find("content-length");
            if (it == current_.headers.end()) {
                // Try case-insensitive
                for (auto& [k, v] : current_.headers) {
                    std::string kl = k;
                    std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
                    if (kl == "content-length") { contentLength_ = std::stoull(v); break; }
                }
            } else {
                contentLength_ = std::stoull(it->second);
            }

            state_ = State::Body;
            return ParseBody();
        }

        auto colon = line.find(':');
        if (colon == std::string::npos) { state_ = State::Error; return ParseResult::Error; }
        std::string key   = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        // Trim leading whitespace from value
        while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0, 1);
        current_.headers[key] = value;
    }
}

ParseResult HttpParser::ParseBody() {
    if (contentLength_ == 0) {
        state_ = State::Complete;
        return ParseResult::Complete;
    }
    size_t available = buffer_.size();
    size_t needed    = contentLength_ - bodyBytesRead_;
    size_t take      = std::min(available, needed);

    current_.body.append(buffer_, 0, take);
    buffer_.erase(0, take);
    consumed_       += take;
    bodyBytesRead_  += take;

    if (bodyBytesRead_ >= contentLength_) {
        state_ = State::Complete;
        return ParseResult::Complete;
    }
    return ParseResult::Incomplete;
}

HttpRequest HttpParser::TakeRequest() {
    HttpRequest req = std::move(current_);
    Reset();
    return req;
}

void HttpParser::Reset() {
    current_       = {};
    contentLength_ = 0;
    bodyBytesRead_ = 0;
    consumed_      = 0;
    state_         = State::RequestLine;
    // Keep any unparsed bytes in buffer_ for pipelining
}

} // namespace orcdb
