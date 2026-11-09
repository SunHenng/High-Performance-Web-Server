#include "http/http_parser.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <string_view>

namespace Http
{
    bool HttpParser::Parse(std::span<const char> data, HttpRequest &request)
    {
        const size_t len = data.size();
        if (len == 0)
            return false;

        // 从中断处继续：parsed_bytes_ 记录本次请求已消费的字节数。
        // 调用方（SubReactor::OnRead）每次传入的都是「累积后的完整缓冲区」，
        // 因此必须跳过已消费的前缀——否则上一轮已解析完的请求行会被当成头部行
        // 再解析一次，而该行内没有 ':' → ParseHeaderLine 返回 false → 整个请求 400。
        const size_t offset = parsed_bytes_ <= len ? parsed_bytes_ : len;
        const char *start = data.data() + offset;
        const char *end = data.data() + len;

        while (start < end && state_ != ParseState::COMPLETE && state_ != ParseState::ERROR && state_ != ParseState::BODY)
        {
            // 查找行结束符
            const char *line_end = nullptr;
            for (const char *p = start; p < end - 1; ++p)
            {
                if (p[0] == '\r' && p[1] == '\n')
                {
                    line_end = p;
                    break;
                }
            }

            if (line_end != nullptr)
            {
                // 找到行结束符
                current_line_.assign(start, line_end - start);
                size_t consumed = (line_end - start) + 2; // 包括\r\n
                start = line_end + 2; // 跳过\r\n

                switch (state_)
                {
                case ParseState::REQUEST_LINE:
                    if (!ParseRequestLine(request))
                    {
                        state_ = ParseState::ERROR;
                        return false;
                    }
                    state_ = ParseState::HEADERS;
                    parsed_bytes_ += consumed;
                    current_line_.clear();
                    break;

                case ParseState::HEADERS:
                    if (current_line_.empty())
                    {
                        // 空行表示头部结束
                        state_ = ParseState::BODY;
                        parsed_bytes_ += consumed;
                        current_line_.clear();

                        // 对于GET请求，如果没有Content-Length，立即标记为完成
                        content_length_ = request.GetContentLength();
                        if (content_length_ == 0)
                        {
                            state_ = ParseState::COMPLETE;
                            request.SetComplete(true);
                        }
                    }
                    else
                    {
                        if (!ParseHeaders(request))
                        {
                            state_ = ParseState::ERROR;
                            return false;
                        }
                        parsed_bytes_ += consumed;
                        current_line_.clear();
                    }
                    break;

                case ParseState::BODY:
                    // 在BODY状态下，我们不应该按行解析
                    break;

                default:
                    break;
                }
            }
            else
            {
                // 本行还没收到完整的 \r\n：本轮就此打住，parsed_bytes_ 保持不变。
                // 缓冲区是唯一数据源——下一轮会用更长的缓冲区从 data + parsed_bytes_
                // 重新扫到这一行，所以这里绝不能把残段累进 current_line_，
                // 否则会和下一轮重新扫到的同一段内容重复拼接。
                start = end;
            }
        }

        // 处理BODY状态
        if (state_ == ParseState::BODY)
        {
            content_length_ = request.GetContentLength();
            if (content_length_ == 0)
            {
                state_ = ParseState::COMPLETE;
                request.SetComplete(true);
            }
            else
            {
                // 请求体数据开始位置。注意这里必须用「当前」parsed_bytes_ 而不是函数
                // 入口算出的 offset——上面的循环可能刚刚把 parsed_bytes_ 推进到
                // 头部结束处。夹一次上限，避免状态异常时 end - body_start 下溢成巨大值。
                const size_t body_offset = parsed_bytes_ <= len ? parsed_bytes_ : len;
                const char *body_start = data.data() + body_offset;

                // 计算剩余需要读取的字节数
                size_t remaining = content_length_ - body_parsed_;
                // 计算当前可以读取的字节数（不超过剩余字节数和当前数据长度）
                size_t to_read = std::min(remaining, static_cast<size_t>(end - body_start));

                if (to_read > 0)
                {
                    // 第一片整体搬入，后续分片直接 AppendBody 追加——不再取出整段 body
                    // 拼好再整体塞回，避免分片到达时每次重拷整个请求体的 O(n²)。
                    const std::string_view body_part(body_start, to_read);
                    if (body_parsed_ == 0)
                    {
                        request.SetBody(std::string(body_part));
                    }
                    else
                    {
                        request.AppendBody(body_part);
                    }
                    body_parsed_ += to_read;
                    parsed_bytes_ += to_read;
                }

                if (body_parsed_ >= content_length_)
                {
                    state_ = ParseState::COMPLETE;
                    request.SetComplete(true);
                }
            }
        }

        return state_ != ParseState::ERROR;
    }

    void HttpParser::Reset()
    {
        state_ = ParseState::REQUEST_LINE;
        current_line_.clear();
        content_length_ = 0;
        parsed_bytes_ = 0;
        body_parsed_ = 0;
    }

    bool HttpParser::ParseRequestLine(HttpRequest& request)
    {
        std::istringstream ss(current_line_);
        std::string method_str, url, version_str;

        if (!(ss >> method_str >> url >> version_str))
        {
            return false;
        }

        return request.ParseRequestLine(current_line_);
    }

    bool HttpParser::ParseHeaders(HttpRequest& request)
    {
        return request.ParseHeaderLine(current_line_);
    }
}