#pragma once
#include <string>
#include <unordered_map>

namespace orcdb {

class HttpResponse {
public:
    int    statusCode{200};
    std::string statusText{"OK"};
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    // Convenience constructors
    static HttpResponse Ok(const std::string& body, const std::string& contentType = "application/json");
    static HttpResponse Created(const std::string& body);
    static HttpResponse NoContent();
    static HttpResponse BadRequest(const std::string& msg);
    static HttpResponse Unauthorized(const std::string& msg = "Unauthorized");
    static HttpResponse Forbidden(const std::string& msg = "Forbidden");
    static HttpResponse NotFound(const std::string& msg = "Not Found");
    static HttpResponse Conflict(const std::string& msg);
    static HttpResponse InternalError(const std::string& msg);
    static HttpResponse ServiceUnavailable(const std::string& msg);
    static HttpResponse TemporaryRedirect(const std::string& location);

    // Serialise to HTTP/1.1 wire format
    std::string Serialise(bool keepAlive = true) const;

    HttpResponse& SetHeader(const std::string& key, const std::string& value);
    HttpResponse& SetBody(const std::string& b, const std::string& contentType = "application/json");

private:
    static std::string StatusText(int code);
};

} // namespace orcdb
