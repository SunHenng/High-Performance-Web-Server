#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <fstream>
#include <filesystem>

#include <thread>
#include "thread/thread_pool.hpp"
#include "Reactors/event_loop.hpp"
// http
#include "http/http_request.hpp"
#include "http/http_response.hpp"
#include "http/http_parser.hpp"

namespace Reactors
{
    struct TcpConnection;

    using TcpConnectionPtr = std::shared_ptr<struct TcpConnection>;

    struct TcpConnection
    {
        int fd;
        sockaddr_in client_addr;
        Http::HttpRequest request;
        Http::HttpResponse response;
        Http::HttpParser parser;
        bool closed = false;
        std::string read_buffer;
        std::string write_buffer;                              // 待发送数据缓冲区
        size_t write_offset{0};                                // 已发送字节偏移
        std::chrono::steady_clock::time_point last_activity;   // 最后活动时间

        TcpConnection(int fd_, const sockaddr_in &addr_)
            : fd(fd_), client_addr(addr_), request(), response(), parser(),
              last_activity(std::chrono::steady_clock::now()) {}
    };

    class SubReactor
    {
    private:
        EventLoop sub_loop_;
        std::shared_ptr<WorkStealingPool> cpu_thread_pool_;

        // 待处理连接队列
        std::queue<std::pair<int, sockaddr_in>> pending_fds_;
        std::mutex queue_mutex_;

        std::unordered_map<int, TcpConnectionPtr> connections_;
        std::atomic<size_t> connection_count_{0};

        // 静态文件根目录
        std::string root_dir_;

        // 空闲连接超时配置
        int idle_timeout_seconds_{30};
        int idle_check_timer_id_{-1}; // 空闲检查定时器ID

    public:
        SubReactor(const std::string &root_dir = "./static/", int idle_timeout_seconds = 30);
        ~SubReactor();

        // 启动/停止子Reactor的事件循环
        void Start();
        void Stop();

        size_t GetConnectionCount() const;
        void AddConnection(int client_fd, const sockaddr_in &addr);
        void HandlePendingConnections();

    private:
        void CreateConnection(int fd, const sockaddr_in &addr);
        void OnRead(TcpConnectionPtr conn);
        void OnWrite(TcpConnectionPtr conn);
        void OnHttpRequest(TcpConnectionPtr conn);
        void HandleAuthRequest(TcpConnectionPtr conn, std::string_view path);
        void SendJsonResponse(TcpConnectionPtr conn, Http::StatusCode status_code, const std::string &json_body);
        bool IsIoTask(const Http::HttpRequest &req);
        void ProcessIoTask(TcpConnectionPtr conn);
        void ProcessCpuTask(TcpConnectionPtr conn);
        void SendResponse(TcpConnectionPtr conn);
        void SendErrorResponse(TcpConnectionPtr conn);
        void CloseConnection(TcpConnectionPtr conn);

        // 空闲连接检查（由定时器周期性调用）
        void CheckIdleConnections();
    };
}
