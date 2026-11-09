#pragma once

#include <string>
#include <span>
#include "http_request.hpp"

namespace Http
{
    // HTTP请求解析器
    class HttpParser
    {
    public:
        HttpParser() : state_(ParseState::REQUEST_LINE), content_length_(0), parsed_bytes_(0), body_parsed_(0) {}

        // 解析请求数据。改用 std::span 表达「只读一段连续字节」——调用方传
        // std::string（read_buffer）或任意连续缓冲区都能隐式转换，无需再传 (指针,长度) 二元组。
        bool Parse(std::span<const char> data, HttpRequest &request);

        // 重置解析器状态
        void Reset();

        // 获取已解析的字节数
        size_t GetParsedBytes() const { return parsed_bytes_; }

        // 检查是否需要更多数据
        bool NeedMoreData() const { return state_ != ParseState::COMPLETE && state_ != ParseState::ERROR; }

        // 检查解析是否出错
        bool HasError() const { return state_ == ParseState::ERROR; }

    private:
        // 解析状态枚举
        enum class ParseState
        {
            REQUEST_LINE, // 解析请求行
            HEADERS,      // 解析头部
            BODY,         // 解析请求体
            COMPLETE,     // 解析完成
            ERROR         // 解析出错
        };

        // 解析请求行
        bool ParseRequestLine(HttpRequest &request);

        // 解析头部
        bool ParseHeaders(HttpRequest &request);

    private:
        ParseState state_;         // 当前解析状态
        std::string current_line_; // 当前正在解析的行
        size_t content_length_;    // 内容长度
        size_t parsed_bytes_;      // 已解析的字节数
        size_t body_parsed_;       // 已解析的请求体字节数
    };
}
