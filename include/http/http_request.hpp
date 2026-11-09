#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include "http_protocol.hpp"

namespace Http
{
    // HTTP请求类
    class HttpRequest
    {
    public:
        HttpRequest() : method_(Method::UNKNOWN), version_(Version::UNKNOWN), content_length_(0), complete_(false) {}

        // 解析请求行
        bool ParseRequestLine(const std::string &line);

        // 解析请求头部
        bool ParseHeaderLine(std::string_view line);

        // 重置为「还没解析任何请求」的状态。
        // Keep-Alive 连接上，一个请求处理完毕、准备接收下一个请求时必须调用它，
        // 否则上一请求的 content_length_ / headers_ 会残留（原因见 .cpp 实现处）。
        void Reset();

        // 设置请求体（右值移动，避免拷贝）
        void SetBody(std::string &&body) { body_ = std::move(body); }

        // 追加请求体片段。分片到达时直接 append，避免旧的「取出整段→拼接→整体塞回」
        // 那种每来一片就重拷整个 body 的 O(n²) 写法。
        void AppendBody(std::string_view part) { body_.append(part.data(), part.size()); }

        // 获取方法
        Method GetMethod() const { return method_; }

        // 获取URL
        const std::string &GetUrl() const { return url_; }

        // 获取URL路径部分
        const std::string &GetPath() const { return path_; }

        // 获取版本
        Version GetVersion() const { return version_; }

        // 获取头部字段
        const std::unordered_map<std::string, std::string> &GetHeaders() const { return headers_; }

        // 获取特定头部字段。返回指向 headers_ 内部的 string_view（零拷贝），
        // 生命周期随 headers_ 覆盖调用方；找不到时返回空 string_view。
        std::string_view GetHeader(std::string_view key) const;

        // 获取请求体
        const std::string &GetBody() const { return body_; }

        // 获取内容长度
        size_t GetContentLength() const { return content_length_; }

        // 检查是否是完整的请求
        bool IsComplete() const { return complete_; }

        // 设置请求为完整
        void SetComplete(bool complete = true) { complete_ = complete; }

    private:
        // 解析URL
        bool ParseUrl(const std::string &url);

    private:
        Method method_;                                        // 请求方法
        std::string url_;                                      // 请求URL
        std::string path_;                                     // URL路径部分
        std::string query_;                                    // URL查询部分
        Version version_;                                      // HTTP版本
        std::unordered_map<std::string, std::string> headers_; // 请求头部
        std::string body_;                                     // 请求体
        size_t content_length_;                                // 内容长度
        bool complete_;                                        // 请求是否完整
    };
}
