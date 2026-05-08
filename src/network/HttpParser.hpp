#pragma once
#include <string>
#include <unordered_map>
#include <optional>

namespace orcdb {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version{"HTTP/1.1"};
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::unordered_map<std::string, std::string> queryParams;
    std::unordered_map<std::string, std::string> pathParams; // filled by Router

    std::string GetHeader(const std::string& name) const;
    std::optional<std::string> GetQueryParam(const std::string& key) const;
    bool IsKeepAlive() const;
};

enum class ParseResult { Complete, Incomplete, Error };

class HttpParser {
public:
    HttpParser() = default;

    ParseResult Feed(const char* data, size_t len);
    ParseResult Feed(const std::string& data);

    bool HasCompleteRequest() const { return state_ == State::Complete; }
    HttpRequest TakeRequest();
    void Reset();

    size_t BytesConsumed() const { return consumed_; }

private:
    enum class State { RequestLine, Headers, Body, Complete, Error };
    State state_{State::RequestLine};

    std::string  buffer_;
    HttpRequest  current_;
    size_t       contentLength_{0};
    size_t       bodyBytesRead_{0};
    size_t       consumed_{0};

    ParseResult ParseRequestLine();
    ParseResult ParseHeaders();
    ParseResult ParseBody();
    void        ParseQueryString(const std::string& rawQuery);
};

} // namespace orcdb
