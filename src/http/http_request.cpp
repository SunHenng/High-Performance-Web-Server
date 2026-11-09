#include "http/http_request.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <system_error>
#include <sstream>

using namespace Http;

bool HttpRequest::ParseRequestLine(const std::string& line) {
    std::istringstream iss(line);
    std::string method_str, version_str;
    
    if (!(iss >> method_str >> url_ >> version_str)) {
        return false;
    }
    
    // 解析方法。必须覆盖 Method 枚举里的全部标准方法——漏掉任何一个都会落到
    // UNKNOWN，而 UNKNOWN 在 SubReactor::IsIoTask() 里等同于"非 OPTIONS"，
    // 会被当成静态文件请求去磁盘找同名文件，最终返回 404。
    // OPTIONS 尤其不能漏：CORS 预检依赖它（api_test.sh 第 7 组回归此点）。
    if (method_str == "GET") {
        method_ = Method::GET;
    } else if (method_str == "POST") {
        method_ = Method::POST;
    } else if (method_str == "PUT") {
        method_ = Method::PUT;
    } else if (method_str == "DELETE") {
        method_ = Method::DELETE;
    } else if (method_str == "HEAD") {
        method_ = Method::HEAD;
    } else if (method_str == "OPTIONS") {
        method_ = Method::OPTIONS;
    } else if (method_str == "PATCH") {
        method_ = Method::PATCH;
    } else if (method_str == "TRACE") {
        method_ = Method::TRACE;
    } else if (method_str == "CONNECT") {
        method_ = Method::CONNECT;
    } else {
        method_ = Method::UNKNOWN;
    }
    
    // 解析版本
    if (version_str == "HTTP/1.0") {
        version_ = Version::HTTP_1_0;
    } else if (version_str == "HTTP/1.1") {
        version_ = Version::HTTP_1_1;
    } else {
        version_ = Version::UNKNOWN;
    }
    
    // 解析URL
    if (!ParseUrl(url_)) {
        return false;
    }
    
    return true;
}

bool HttpRequest::ParseHeaderLine(std::string_view line) {
    auto colon_pos = line.find(':');
    if (colon_pos == std::string_view::npos) {
        return false;
    }

    std::string_view key = line.substr(0, colon_pos);
    std::string_view value = line.substr(colon_pos + 1);

    // 修剪空格（返回 string_view 视图，不分配）。注意 find_first/last_not_of 返回
    // npos 的边界：全空白时要显式返回空视图，否则 npos+1 会回绕成 0 把整串截没。
    auto trim_front = [](std::string_view sv) -> std::string_view {
        const size_t first = sv.find_first_not_of(" \t");
        return first == std::string_view::npos ? std::string_view{} : sv.substr(first);
    };
    auto trim_back = [](std::string_view sv) -> std::string_view {
        const size_t last = sv.find_last_not_of(" \t");
        return last == std::string_view::npos ? std::string_view{} : sv.substr(0, last + 1);
    };
    key = trim_back(trim_front(key));
    value = trim_back(trim_front(value));

    // 落地：key 转小写存为 string（headers_ 的键必须小写，便于 GetHeader 大小写不敏感查找），
    // value 直接在原地构造 string。
    if (key.empty() || std::any_of(key.begin(), key.end(), [](unsigned char ch) {
        return std::isspace(ch) || ch < 0x21 || ch == ':';
    })) {
        return false;
    }

    std::string key_str(key);
    std::transform(key_str.begin(), key_str.end(), key_str.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    // 检查Content-Length
    if (key_str == "content-length") {
        size_t parsed = 0;
        const char *first = value.data();
        const char *last = first + value.size();
        const auto result = std::from_chars(first, last, parsed);
        if (result.ec != std::errc{} || result.ptr != last) {
            return false;
        }
        const auto existing = headers_.find(key_str);
        if (existing != headers_.end()) {
            size_t previous = 0;
            const char *old_first = existing->second.data();
            const char *old_last = old_first + existing->second.size();
            const auto old = std::from_chars(old_first, old_last, previous);
            if (old.ec != std::errc{} || old.ptr != old_last || previous != parsed) {
                return false;
            }
        }
        content_length_ = parsed;
    }

    headers_[key_str] = std::string(value);

    return true;
}

// 把请求对象恢复到「还没解析任何请求」的初始状态。
//
// 为什么必须有这个函数：HttpRequest 是 TcpConnection 的成员，**一条连接上的所有
// 请求共用同一个对象**。而 ParseRequestLine() 只覆盖 method_/url_/version_，
// ParseHeaderLine() 只做 headers_[key] = value 的覆盖写——两者都不清理上一请求
// 留下的字段。于是残留的 content_length_ 会让解析器误判新请求也带请求体：
//
//   POST /register (Content-Length: 40)   ← content_length_ = 40
//   响应发完，连接复用                    ← 此时只 Reset 了 parser 和 response
//   GET /login.html（无请求体）           ← 继承 40 → 解析器停在 BODY 状态
//                                        ← 等一个永远不来的请求体
//                                        ← NeedMoreData() 恒为真
//                                        ← OnHttpRequest() 永不被调用 → 浏览器挂死
//
// 调用时机只有一处：SubReactor::OnWrite() 里响应发完、确认要复用连接之后。
// **不能**放在 OnRead() 解析之前——请求是跨多次 read 增量解析的，放在那里会在
// 解析中途把已经解析好的部分清掉。
void HttpRequest::Reset() {
    method_ = Method::UNKNOWN;
    version_ = Version::UNKNOWN;
    url_.clear();
    path_.clear();
    query_.clear();
    headers_.clear();
    body_.clear();
    content_length_ = 0;
    complete_ = false;
}

std::string_view HttpRequest::GetHeader(std::string_view key) const {
    std::string lower_key(key);
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    auto it = headers_.find(lower_key);
    if (it != headers_.end()) {
        return it->second;
    }
    return {};
}

bool HttpRequest::ParseUrl(const std::string& url) {
    // 简单的URL解析，只处理path和query
    auto query_pos = url.find('?');
    if (query_pos != std::string::npos) {
        path_ = url.substr(0, query_pos);
        query_ = url.substr(query_pos + 1);
    } else {
        path_ = url;
        query_.clear();
    }
    
    // 确保path以/开头
    if (path_.empty() || path_[0] != '/') {
        path_ = "/" + path_;
    }
    
    return true;
}
