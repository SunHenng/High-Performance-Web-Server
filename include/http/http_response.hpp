#pragma once

#include <string>
#include <unordered_map>
#include "http_protocol.hpp"

namespace Http
{
    // HTTP响应类
    class HttpResponse
    {
    public:
        HttpResponse() : status_code_(StatusCode::OK), version_(Version::HTTP_1_1) {}

        // 设置状态码
        void SetStatusCode(StatusCode code) { status_code_ = code; }

        // 获取状态码
        StatusCode GetStatusCode() const { return status_code_; }

        // 设置HTTP版本
        void SetVersion(Version version) { version_ = version; }

        // 获取HTTP版本
        Version GetVersion() const { return version_; }

        // 添加头部字段
        void AddHeader(const std::string &key, const std::string &value) { headers_[key] = value; }

        // 设置头部字段
        void SetHeaders(const std::unordered_map<std::string, std::string> &headers) { headers_ = headers; }

        // 获取头部字段
        const std::unordered_map<std::string, std::string> &GetHeaders() const { return headers_; }

        // 设置响应体
        void SetBody(const std::string &body) { body_ = body; }

        // 获取响应体
        const std::string &GetBody() const { return body_; }

        // 构建响应字符串
        std::string BuildResponse() const;

        // 转换为字符串（与BuildResponse相同功能，提供方便的接口）
        std::string ToString() const { return BuildResponse(); }

        // 设置内容类型
        void SetContentType(const std::string &content_type) { AddHeader("Content-Type", content_type); }

        // 设置内容长度
        void SetContentLength(size_t length) { AddHeader("Content-Length", std::to_string(length)); }

        void Reset()
        {
            status_code_ = StatusCode::OK;
            version_ = Version::HTTP_1_1;
            headers_.clear();
            body_.clear();
        }

    private:
        // 获取状态消息
        std::string GetStatusMessage() const;

        // 获取版本字符串
        std::string GetVersionString() const;

    private:
        StatusCode status_code_;                               // 状态码
        Version version_;                                      // HTTP版本
        std::unordered_map<std::string, std::string> headers_; // 响应头部
        std::string body_;                                     // 响应体
    };
}
