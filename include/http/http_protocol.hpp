#pragma once

#include <string>
#include <unordered_map>

namespace Http
{
    // HTTP方法
    enum class Method
    {
        GET,
        POST,
        PUT,
        DELETE,
        HEAD,
        OPTIONS,
        PATCH,
        TRACE,
        CONNECT,
        UNKNOWN
    };

    // HTTP状态码
    enum class StatusCode
    {
        OK = 200,
        Created = 201,
        Accepted = 202,
        NoContent = 204,
        BadRequest = 400,
        PayloadTooLarge = 413,
        Unauthorized = 401,
        Forbidden = 403,
        NotFound = 404,
        MethodNotAllowed = 405,
        InternalServerError = 500,
        NotImplemented = 501,
        BadGateway = 502,
        ServiceUnavailable = 503
    };

    // HTTP版本
    enum class Version
    {
        HTTP_1_0,
        HTTP_1_1,
        HTTP_2_0,
        UNKNOWN
    };

    // 这里曾声明 kMimeTypes / kStatusMessages / kMethodMap / kVersionMap 四个
    // 「字符串→枚举」映射表，但四者都只有声明、没有定义、也没有任何引用（悬空声明）。
    // 实际的方法识别写在 http_request.cpp:ParseRequestLine()，状态行文案写在
    // http_response.cpp，MIME 类型写在 SubReactor.cpp:GetContentType()。
    // 若将来要改成表驱动，删掉的那四个声明需要连同定义一起补回。
}
