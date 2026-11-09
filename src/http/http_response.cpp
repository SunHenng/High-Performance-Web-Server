#include "http/http_response.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>

using namespace Http;

namespace
{
    // 大小写不敏感地判断某个响应头是否已设置
    bool HasHeader(const std::unordered_map<std::string, std::string> &headers, const std::string &name)
    {
        return std::ranges::any_of(headers,
                                   [&name](const auto &pair)
                                   {
                                       const std::string &key = pair.first;
                                       return key.size() == name.size() &&
                                              std::equal(key.begin(), key.end(), name.begin(),
                                                         [](unsigned char a, unsigned char b)
                                                         { return std::tolower(a) == std::tolower(b); });
                                   });
    }
}

std::string HttpResponse::BuildResponse() const {
    std::ostringstream oss;

    // 状态行
    oss << GetVersionString() << " " << static_cast<int>(GetStatusCode()) << " " << GetStatusMessage() << "\r\n";

    // 头部
    for (const auto& [key, value] : headers_) {
        oss << key << ": " << value << "\r\n";
    }

    // 未显式设置 Content-Length 时按响应体长度补齐。
    // 否则在 Keep-Alive 连接上响应既无 Content-Length 也无 Transfer-Encoding，
    // 客户端无法判断消息边界，只能一直读到连接关闭（默认要等空闲超时）。
    if (!HasHeader(headers_, "Content-Length")) {
        oss << "Content-Length: " << body_.size() << "\r\n";
    }

    // 空行
    oss << "\r\n";

    // 响应体
    oss << body_;

    return oss.str();
}

std::string HttpResponse::GetVersionString() const {
    switch (version_) {
        case Version::HTTP_1_0:
            return "HTTP/1.0";
        case Version::HTTP_1_1:
            return "HTTP/1.1";
        default:
            return "HTTP/1.1";
    }
}

std::string HttpResponse::GetStatusMessage() const {
    switch (status_code_) {
        case StatusCode::OK:                  return "OK";
        case StatusCode::Created:             return "Created";
        case StatusCode::Accepted:            return "Accepted";
        case StatusCode::NoContent:           return "No Content";
        case StatusCode::BadRequest:          return "Bad Request";
        case StatusCode::PayloadTooLarge:     return "Payload Too Large";
        case StatusCode::Unauthorized:        return "Unauthorized";
        case StatusCode::Forbidden:           return "Forbidden";
        case StatusCode::NotFound:            return "Not Found";
        case StatusCode::MethodNotAllowed:    return "Method Not Allowed";
        case StatusCode::InternalServerError: return "Internal Server Error";
        case StatusCode::NotImplemented:      return "Not Implemented";
        case StatusCode::BadGateway:          return "Bad Gateway";
        case StatusCode::ServiceUnavailable:  return "Service Unavailable";
        default:
            return "Unknown Status";
    }
}
