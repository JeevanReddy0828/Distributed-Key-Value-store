#include "HttpResponse.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

namespace orcdb {

using json = nlohmann::json;

static std::string JsonError(const std::string& msg) {
    return json{{"error", msg}}.dump();
}

HttpResponse HttpResponse::Ok(const std::string& body, const std::string& contentType) {
    HttpResponse r;
    r.statusCode = 200;
    r.body       = body;
    r.headers["Content-Type"] = contentType;
    return r;
}

HttpResponse HttpResponse::Created(const std::string& body) {
    HttpResponse r;
    r.statusCode = 201;
    r.body       = body;
    r.headers["Content-Type"] = "application/json";
    return r;
}

HttpResponse HttpResponse::NoContent() {
    HttpResponse r;
    r.statusCode = 204;
    return r;
}

HttpResponse HttpResponse::BadRequest(const std::string& msg) {
    HttpResponse r;
    r.statusCode = 400;
    r.body       = JsonError(msg);
    r.headers["Content-Type"] = "application/json";
    return r;
}

HttpResponse HttpResponse::Unauthorized(const std::string& msg) {
    HttpResponse r;
    r.statusCode = 401;
    r.body       = JsonError(msg);
    r.headers["Content-Type"] = "application/json";
    r.headers["WWW-Authenticate"] = "Bearer realm=\"orcdb\"";
    return r;
}

HttpResponse HttpResponse::Forbidden(const std::string& msg) {
    HttpResponse r;
    r.statusCode = 403;
    r.body       = JsonError(msg);
    r.headers["Content-Type"] = "application/json";
    return r;
}

HttpResponse HttpResponse::NotFound(const std::string& msg) {
    HttpResponse r;
    r.statusCode = 404;
    r.body       = JsonError(msg);
    r.headers["Content-Type"] = "application/json";
    return r;
}

HttpResponse HttpResponse::Conflict(const std::string& msg) {
    HttpResponse r;
    r.statusCode = 409;
    r.body       = JsonError(msg);
    r.headers["Content-Type"] = "application/json";
    return r;
}

HttpResponse HttpResponse::InternalError(const std::string& msg) {
    HttpResponse r;
    r.statusCode = 500;
    r.body       = JsonError(msg);
    r.headers["Content-Type"] = "application/json";
    return r;
}

HttpResponse HttpResponse::ServiceUnavailable(const std::string& msg) {
    HttpResponse r;
    r.statusCode = 503;
    r.body       = JsonError(msg);
    r.headers["Content-Type"] = "application/json";
    return r;
}

HttpResponse HttpResponse::TemporaryRedirect(const std::string& location) {
    HttpResponse r;
    r.statusCode = 307;
    r.headers["Location"] = location;
    return r;
}

HttpResponse& HttpResponse::SetHeader(const std::string& key, const std::string& value) {
    headers[key] = value;
    return *this;
}

HttpResponse& HttpResponse::SetBody(const std::string& b, const std::string& contentType) {
    body = b;
    headers["Content-Type"] = contentType;
    return *this;
}

std::string HttpResponse::StatusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 307: return "Temporary Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

std::string HttpResponse::Serialise(bool keepAlive) const {
    std::ostringstream os;
    os << "HTTP/1.1 " << statusCode << " " << StatusText(statusCode) << "\r\n";

    for (auto& [k, v] : headers) {
        os << k << ": " << v << "\r\n";
    }

    os << "Content-Length: " << body.size() << "\r\n";
    os << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n";
    os << "\r\n";
    os << body;
    return os.str();
}

} // namespace orcdb
