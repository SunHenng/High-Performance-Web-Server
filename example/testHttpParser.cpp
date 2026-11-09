// ModernWebServer — HTTP 解析器与响应构建单元测试
//
// 不依赖 MySQL、不依赖网络，可在任意环境直接运行：
//     ./bin/testHttpParser
//
// 全部用例通过时进程返回 0，存在失败用例时返回 1（可直接接入 CI）。

#include "http/http_parser.hpp"
#include "http/http_response.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace
{
    int g_passed = 0;
    int g_failed = 0;

    void Check(bool cond, const std::string &what)
    {
        if (cond)
        {
            ++g_passed;
            std::cout << "  [PASS] " << what << "\n";
        }
        else
        {
            ++g_failed;
            std::cout << "  [FAIL] " << what << "\n";
        }
    }

    void CheckEq(std::string_view actual, std::string_view expected, const std::string &what)
    {
        if (actual == expected)
        {
            ++g_passed;
            std::cout << "  [PASS] " << what << "\n";
        }
        else
        {
            ++g_failed;
            std::cout << "  [FAIL] " << what
                      << "\n         期望: " << expected
                      << "\n         实际: " << actual << "\n";
        }
    }

    bool Contains(const std::string &haystack, const std::string &needle)
    {
        return haystack.find(needle) != std::string::npos;
    }
}

// ==================== 解析器 ====================

static void TestParseSimpleGet()
{
    std::cout << "\n[解析器] 简单 GET 请求\n";

    const std::string raw =
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    Http::HttpParser parser;
    Http::HttpRequest req;
    const bool ok = parser.Parse(raw, req);

    Check(ok, "Parse() 返回 true");
    Check(!parser.HasError(), "未进入错误状态");
    Check(!parser.NeedMoreData(), "请求已完整");
    Check(req.IsComplete(), "HttpRequest 被标记为完整");
    Check(req.GetMethod() == Http::Method::GET, "方法为 GET");
    CheckEq(req.GetPath(), "/index.html", "路径为 /index.html");
    Check(req.GetVersion() == Http::Version::HTTP_1_1, "版本为 HTTP/1.1");
    CheckEq(req.GetHeader("Connection"), "keep-alive", "Connection 头解析正确");
    CheckEq(req.GetHeader("HOST"), "localhost", "头部键名大小写不敏感");
    Check(parser.GetParsedBytes() == raw.size(), "已消费字节数等于请求总长度");
}

static void TestParseQueryString()
{
    std::cout << "\n[解析器] 带查询串的 URL\n";

    const std::string raw = "GET /search?q=cpp&page=2 HTTP/1.1\r\nHost: x\r\n\r\n";

    Http::HttpParser parser;
    Http::HttpRequest req;
    parser.Parse(raw, req);

    CheckEq(req.GetPath(), "/search", "路径剥掉了查询串");
    CheckEq(req.GetUrl(), "/search?q=cpp&page=2", "原始 URL 完整保留");
}

static void TestParsePostWithBody()
{
    std::cout << "\n[解析器] POST 请求 + Content-Length 请求体\n";

    const std::string body = R"({"username":"testuser","password":"abcdef"})";
    const std::string raw =
        "POST /login HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" + body;

    Http::HttpParser parser;
    Http::HttpRequest req;
    const bool ok = parser.Parse(raw, req);

    Check(ok, "Parse() 返回 true");
    Check(!parser.HasError(), "未进入错误状态");
    Check(req.GetMethod() == Http::Method::POST, "方法为 POST");
    CheckEq(req.GetPath(), "/login", "路径为 /login");
    Check(req.GetContentLength() == body.size(), "Content-Length 解析正确");
    CheckEq(req.GetBody(), body, "请求体完整读取");
    Check(req.IsComplete(), "HttpRequest 被标记为完整");
    Check(parser.GetParsedBytes() == raw.size(), "已消费字节数等于请求总长度");
}

static void TestParseMalformedRequestLine()
{
    std::cout << "\n[解析器] 畸形请求行应当报错\n";

    const std::string raw = "GET\r\n\r\n";

    Http::HttpParser parser;
    Http::HttpRequest req;
    parser.Parse(raw, req);

    Check(parser.HasError(), "只有方法没有 URL 时进入错误状态");
}

static void TestParserResetBetweenRequests()
{
    std::cout << "\n[解析器] Reset 后可复用于下一个请求（Keep-Alive 场景）\n";

    Http::HttpParser parser;

    Http::HttpRequest req1;
    const std::string r1 = "GET /a.html HTTP/1.1\r\nHost: x\r\n\r\n";
    parser.Parse(r1, req1);
    Check(!parser.NeedMoreData(), "第一个请求解析完整");
    CheckEq(req1.GetPath(), "/a.html", "第一个请求路径正确");

    parser.Reset();

    Http::HttpRequest req2;
    const std::string r2 = "GET /b.html HTTP/1.1\r\nHost: x\r\n\r\n";
    const bool ok = parser.Parse(r2, req2);
    Check(ok, "Reset 后第二个请求解析成功");
    CheckEq(req2.GetPath(), "/b.html", "第二个请求路径正确");
    Check(parser.GetParsedBytes() == r2.size(), "第二个请求字节计数已归零重算");
}

// 回归：Method 枚举里的每个标准方法都必须能被解析出来。
// 曾经漏掉 OPTIONS——解析成 UNKNOWN 后，SubReactor::IsIoTask() 会把预检请求
// 当成静态文件请求去磁盘找同名文件，于是 CORS 预检返回 404 而不是 200。
// 由 api_test.sh 第 7 组发现，这条用例把同一缺陷挡在单元测试层。
static void TestParseAllMethods()
{
    std::cout << "\n[解析器] 全部标准 HTTP 方法都能被识别\n";

    struct Case
    {
        const char *token;
        Http::Method expected;
    };

    const Case cases[] = {
        {"GET", Http::Method::GET},
        {"POST", Http::Method::POST},
        {"PUT", Http::Method::PUT},
        {"DELETE", Http::Method::DELETE},
        {"HEAD", Http::Method::HEAD},
        {"OPTIONS", Http::Method::OPTIONS},
        {"PATCH", Http::Method::PATCH},
        {"TRACE", Http::Method::TRACE},
        {"CONNECT", Http::Method::CONNECT},
    };

    for (const Case &c : cases)
    {
        const std::string raw = std::string(c.token) + " /login HTTP/1.1\r\nHost: x\r\n\r\n";

        Http::HttpParser parser;
        Http::HttpRequest req;
        parser.Parse(raw, req);

        Check(req.GetMethod() == c.expected, std::string("方法 ") + c.token + " 被识别");
    }

    // 未定义的方法应当回落到 UNKNOWN，而不是被误判成某个已定义方法
    const std::string raw = "FOOBAR /login HTTP/1.1\r\nHost: x\r\n\r\n";
    Http::HttpParser parser;
    Http::HttpRequest req;
    parser.Parse(raw, req);
    Check(req.GetMethod() == Http::Method::UNKNOWN, "未定义方法回落到 UNKNOWN");
}

// ==================== 响应 ====================

static void TestResponseAutoContentLength()
{
    std::cout << "\n[响应] 未显式设置时自动补齐 Content-Length\n";

    const std::string body = R"({"success":true})";
    Http::HttpResponse resp;
    resp.SetStatusCode(Http::StatusCode::OK);
    resp.SetContentType("application/json");
    resp.SetBody(body);

    const std::string out = resp.ToString();

    Check(Contains(out, "HTTP/1.1 200 OK\r\n"), "状态行正确");
    Check(Contains(out, "Content-Length: " + std::to_string(body.size()) + "\r\n"),
          "自动补上等于响应体长度的 Content-Length");
}

static void TestResponseExplicitContentLengthNotDuplicated()
{
    std::cout << "\n[响应] 显式设置的 Content-Length 不会被重复写入\n";

    Http::HttpResponse resp;
    resp.SetContentType("text/html");
    resp.SetContentLength(5);
    resp.SetBody("hello");

    const std::string out = resp.ToString();
    const size_t first = out.find("Content-Length:");

    Check(first != std::string::npos, "存在 Content-Length");
    Check(out.find("Content-Length:", first + 1) == std::string::npos, "Content-Length 只出现一次");
    Check(Contains(out, "Content-Length: 5\r\n"), "取的是显式设置的值");
}

static void TestStatusMessages()
{
    std::cout << "\n[响应] 状态行文案覆盖\n";

    struct Case
    {
        Http::StatusCode code;
        const char *line;
    };

    const Case cases[] = {
        {Http::StatusCode::OK, "HTTP/1.1 200 OK\r\n"},
        {Http::StatusCode::Created, "HTTP/1.1 201 Created\r\n"},
        {Http::StatusCode::NoContent, "HTTP/1.1 204 No Content\r\n"},
        {Http::StatusCode::BadRequest, "HTTP/1.1 400 Bad Request\r\n"},
        {Http::StatusCode::PayloadTooLarge, "HTTP/1.1 413 Payload Too Large\r\n"},
        {Http::StatusCode::Unauthorized, "HTTP/1.1 401 Unauthorized\r\n"},
        {Http::StatusCode::Forbidden, "HTTP/1.1 403 Forbidden\r\n"},
        {Http::StatusCode::NotFound, "HTTP/1.1 404 Not Found\r\n"},
        {Http::StatusCode::MethodNotAllowed, "HTTP/1.1 405 Method Not Allowed\r\n"},
        {Http::StatusCode::InternalServerError, "HTTP/1.1 500 Internal Server Error\r\n"},
        {Http::StatusCode::ServiceUnavailable, "HTTP/1.1 503 Service Unavailable\r\n"},
    };

    for (const Case &c : cases)
    {
        Http::HttpResponse resp;
        resp.SetStatusCode(c.code);
        resp.SetBody("x");
        Check(Contains(resp.ToString(), c.line), std::string("状态行: ") + c.line);
    }
}

static void TestResponseReset()
{
    std::cout << "\n[响应] Reset 清空全部状态\n";

    Http::HttpResponse resp;
    resp.SetStatusCode(Http::StatusCode::NotFound);
    resp.SetContentType("text/html");
    resp.SetBody("原来的响应体");

    resp.Reset();
    resp.SetBody("ok");

    const std::string out = resp.ToString();
    Check(Contains(out, "HTTP/1.1 200 OK\r\n"), "状态码复位为 200");
    Check(!Contains(out, "原来的响应体"), "旧响应体已清空");
    Check(!Contains(out, "text/html"), "旧头部已清空");
    CheckEq(resp.GetBody(), "ok", "新响应体生效");
}

// ==================== 分段到达（回归：修复前稳定返回 400） ====================
// SubReactor::OnRead() 每次把「累积后的完整缓冲区」整体传给 Parse()：
//     conn->read_buffer.append(buff, nread);
//     conn->parser.Parse(conn->read_buffer, conn->request);
//
// 修复前 Parse() 固定从缓冲区偏移 0 开始扫描，不跳过 parsed_bytes_。于是当第一段
// 只收到请求行时就出事：
//   1. 第一轮解析完请求行，state_ = HEADERS；
//   2. 第二轮从偏移 0 重扫，把已解析过的请求行当成头部行，
//      ParseHeaderLine() 在行内找不到 ':' → 返回 false → 整个请求 400。
// 小请求通常能在一次 4096 字节的 read() 内收全，所以压测不暴露；
// 但请求头超过 4096 字节、或客户端分包发送，就会稳定失败。
static void TestParseSplitAcrossSegments()
{
    std::cout << "\n[解析器] 请求跨 TCP 分段到达\n";

    // 段边界落在请求行之后——即上面描述的那条 400 路径
    {
        const std::string part1 = "GET /index.html HTTP/1.1\r\n";
        const std::string part2 = "Host: localhost\r\n\r\n";

        Http::HttpParser parser;
        Http::HttpRequest req;

        parser.Parse(part1, req);
        Check(parser.NeedMoreData(), "只收到请求行时应要求更多数据");

        const std::string whole = part1 + part2;
        const bool ok = parser.Parse(whole, req);
        Check(ok && !parser.HasError(), "头部随下一段到达后应解析成功（不报 400）");
        Check(!parser.NeedMoreData(), "请求已完整");
        CheckEq(req.GetPath(), "/index.html", "路径正确");
        Check(req.GetVersion() == Http::Version::HTTP_1_1, "版本号未被拼接污染");
        CheckEq(req.GetHeader("host"), "localhost", "头部解析正确");
        Check(parser.GetParsedBytes() == whole.size(), "已消费字节数等于请求总长度");
    }

    // 段边界落在请求行中间
    {
        const std::string part1 = "GET /index.html HTT";
        const std::string part2 = "P/1.1\r\nHost: localhost\r\n\r\n";

        Http::HttpParser parser;
        Http::HttpRequest req;

        parser.Parse(part1, req);
        const std::string whole = part1 + part2;
        const bool ok = parser.Parse(whole, req);
        Check(ok && !parser.HasError(), "请求行被截断后重解析应成功");
        Check(req.GetVersion() == Http::Version::HTTP_1_1, "版本号被正确还原");
        CheckEq(req.GetPath(), "/index.html", "路径正确");
    }

    // 段边界落在某个头部行中间
    {
        const std::string part1 = "GET / HTTP/1.1\r\nHost: local";
        const std::string part2 = "host\r\n\r\n";

        Http::HttpParser parser;
        Http::HttpRequest req;

        parser.Parse(part1, req);
        const std::string whole = part1 + part2;
        parser.Parse(whole, req);
        Check(!parser.HasError(), "头部行被截断后不应报错");
        CheckEq(req.GetHeader("host"), "localhost", "被截断的头部值正确拼接");
    }

    // 按 4096 字节切片喂入，复现 OnRead 里 read() 缓冲区的大小
    {
        std::string raw = "GET /index.html HTTP/1.1\r\n";
        for (int i = 0; i < 200; ++i)
        {
            raw += "X-Pad-" + std::to_string(i) + ": " + std::string(20, 'a') + "\r\n";
        }
        raw += "\r\n";

        Http::HttpParser parser;
        Http::HttpRequest req;
        std::string acc;

        for (size_t off = 0; off < raw.size(); off += 4096)
        {
            const size_t n = (raw.size() - off < 4096) ? (raw.size() - off) : 4096;
            acc.append(raw, off, n);
            parser.Parse(acc, req);
        }

        Check(!parser.HasError(), "超过 4096 字节的请求头分片喂入不应报错");
        Check(!parser.NeedMoreData(), "分片喂完后请求已完整");
        CheckEq(req.GetPath(), "/index.html", "分片场景路径正确");
        CheckEq(req.GetHeader("x-pad-199"), std::string(20, 'a'), "最后一行头部完整");
    }
}

// 回归：Keep-Alive 连接上复用**同一个** HttpRequest 对象时，Content-Length 残留。
//
// 上面 TestParserResetBetweenRequests() 那条用例每次都用新的 HttpRequest，
// 而 SubReactor 里 conn->request 是整条连接共用的一个对象——用例构造方式和
// 真实调用方式不一致，于是把本缺陷整个绕开了。这条用例刻意只用一个对象。
//
// 缺陷链条：POST 带 Content-Length: 40 → 响应发完只 Reset 了 parser 和 response
// → 紧随其后的 GET（无请求体）继承 40 → 解析器停在 BODY 等一个永远不来的请求体
// → NeedMoreData() 恒为真 → 服务端不再回应任何请求。
// 真实症状：浏览器注册成功（绿框）后跳转 /login.html 永久挂起。
// curl 测不出来，因为它默认不发 Connection: keep-alive，每个请求都换新连接。
static void TestReuseRequestObjectAcrossKeepAlive()
{
    std::cout << "\n[解析器] Keep-Alive 连接复用同一个请求对象\n";

    Http::HttpParser parser;
    Http::HttpRequest req; // 整个用例只此一个对象，模拟 conn->request

    // 第一个请求：带请求体的 POST —— 这次解析会把 content_length_ 置为 40
    const std::string post =
        "POST /register HTTP/1.1\r\n"
        "Host: x\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 40\r\n"
        "\r\n"
        "{\"username\":\"bob\",\"password\":\"abcdefgh\"}"; // 恰好 40 字节
    Check(parser.Parse(post, req), "POST 请求解析成功");
    Check(!parser.NeedMoreData(), "POST 请求解析完整");
    CheckEq(req.GetPath(), "/register", "POST 路径正确");
    Check(req.GetContentLength() == 40, "POST 的 Content-Length 已记录为 40");

    // 响应发送完毕后，SubReactor 会连续复位这三个对象（顺序照抄 OnWrite）
    parser.Reset();
    req.Reset();

    // 第二个请求：浏览器跳转产生的无请求体 GET，同一个对象
    const std::string get = "GET /login.html HTTP/1.1\r\nHost: x\r\n\r\n";
    Check(parser.Parse(get, req), "GET 请求解析成功");
    Check(!parser.NeedMoreData(),
          "GET 请求解析完整（未继承上一请求的 Content-Length）");
    CheckEq(req.GetPath(), "/login.html", "GET 路径正确");
    Check(req.GetContentLength() == 0, "GET 的 Content-Length 已归零");
    Check(parser.GetParsedBytes() == get.size(), "GET 的已消费字节数正确");

    // 第三个请求：再来一个带请求体的 POST，确认 Content-Length 是新值而非旧值
    parser.Reset();
    req.Reset();

    const std::string post2 =
        "POST /login HTTP/1.1\r\n"
        "Host: x\r\n"
        "Content-Length: 7\r\n"
        "\r\n"
        "abcdefg";
    Check(parser.Parse(post2, req), "第二个 POST 解析成功");
    Check(!parser.NeedMoreData(), "第二个 POST 解析完整");
    Check(req.GetContentLength() == 7, "Content-Length 已更新为 7 而非残留的 40");
    CheckEq(req.GetBody(), "abcdefg", "第二个 POST 的请求体正确");
}

static void TestRejectMalformedContentLength()
{
    std::cout << "\n[解析器] 拒绝非法或冲突的 Content-Length\n";
    const char *bad[] = {"Content-Length: nope", "Content-Length: 4x",
                         "Content-Length: 184467440737095516160", " : value"};
    for (const char *header : bad)
    {
        Http::HttpParser parser;
        Http::HttpRequest req;
        const std::string raw = std::string("POST / HTTP/1.1\r\nHost: x\r\n") +
                                header + "\r\n\r\n";
        parser.Parse(raw, req);
        Check(parser.HasError(), std::string("拒绝: ") + header);
    }

    Http::HttpParser parser;
    Http::HttpRequest req;
    const std::string conflicting =
        "POST / HTTP/1.1\r\nHost: x\r\n"
        "Content-Length: 3\r\nContent-Length: 4\r\n\r\nabc";
    parser.Parse(conflicting, req);
    Check(parser.HasError(), "拒绝冲突的重复 Content-Length");
}

int main()
{
    std::cout << "==== ModernWebServer HTTP 单元测试 ====\n";

    TestParseSimpleGet();
    TestParseQueryString();
    TestParsePostWithBody();
    TestParseMalformedRequestLine();
    TestRejectMalformedContentLength();
    TestParserResetBetweenRequests();
    TestParseAllMethods();

    TestResponseAutoContentLength();
    TestResponseExplicitContentLengthNotDuplicated();
    TestStatusMessages();
    TestResponseReset();

    TestParseSplitAcrossSegments();
    TestReuseRequestObjectAcrossKeepAlive();

    std::cout << "\n=======================================\n";
    std::cout << "通过 " << g_passed << " 项，失败 " << g_failed << " 项\n";
    std::cout << "=======================================\n";

    return g_failed == 0 ? 0 : 1;
}
