#pragma once
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>  // for fcntl
#include <errno.h>
#include <netinet/tcp.h> // for TCP_NODELAY

#include <string.h> // for memset
#include <iostream> // for std::cerr
#include <arpa/inet.h> // for inet_ntoa, ntohs

#include "Reactors/event_loop.hpp"
#include "Reactors/LoadBalancer.hpp"
#include "Reactors/SubReactor.hpp"
#include "log/Logger.hpp"

// 主Reactor：只负责accept新连接
namespace Reactors
{
    class MainReactor
    {
    private:
        EventLoop main_loop_;           // 主事件循环
        LoadBalancer& load_balancer_;   // 负载均衡器
        int listen_fd_;
        std::thread::id main_thread_id;
        bool owns_load_balancer_;       // 标记是否拥有负载均衡器
        int max_connections_{10000};    // 最大并发连接数
        std::function<size_t()> get_total_connections_; // 获取当前总连接数的回调

    public:
        MainReactor(LoadBalancer &load_balancer,
                    int max_connections = 10000,
                    std::function<size_t()> get_conn_cb = nullptr)
            : main_loop_(),
              load_balancer_(load_balancer),
              listen_fd_(-1),
              main_thread_id(std::this_thread::get_id()),
              owns_load_balancer_(false),
              max_connections_(max_connections),
              get_total_connections_(std::move(get_conn_cb))
        {
        }

        MainReactor(int thread_num = std::thread::hardware_concurrency(),
                    int max_connections = 10000,
                    std::function<size_t()> get_conn_cb = nullptr)
            : main_loop_(),
              load_balancer_(*new LoadBalancer(thread_num)),
              listen_fd_(-1),
              main_thread_id(std::this_thread::get_id()),
              owns_load_balancer_(true),
              max_connections_(max_connections),
              get_total_connections_(std::move(get_conn_cb))
        {
        }

        ~MainReactor()
        {
            if (listen_fd_ >= 0)
            {
                close(listen_fd_);
            }
            if (owns_load_balancer_)
            {
                delete &load_balancer_;
            }
        }

        // 设置监听套接字
        bool SetListenSocket(int port, int backlog = SOMAXCONN)
        {
            listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
            if (listen_fd_ < 0)
            {
                perror("socket");
                return false;
            }

            // 设置地址重用
            int opt = 1;
            setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            // SO_REUSEPORT 需要 Linux 3.9+，失败时仅记录日志不阻止启动
            if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
            {
                // 旧内核或 WSL 不支持，降级为仅使用 SO_REUSEADDR
            }

            // 绑定地址
            sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port = htons(port);

            if (bind(listen_fd_, (sockaddr *)&addr, sizeof(addr)) < 0)
            {
                LOG_ERROR << "bind failed on port " << port << ": " << strerror(errno);
                close(listen_fd_);
                listen_fd_ = -1;
                return false;
            }

            // 开始监听
            if (listen(listen_fd_, backlog) < 0)
            {
                LOG_ERROR << "listen failed on port " << port << ": " << strerror(errno);
                close(listen_fd_);
                listen_fd_ = -1;
                return false;
            }

            // 设置监听套接字为非阻塞
            int flags = fcntl(listen_fd_, F_GETFL, 0);
            fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);

            // 注册accept事件
            main_loop_.UpdateChannel(
                listen_fd_,
                EventLoop::kReadEvent,
                std::bind(&MainReactor::OnAccept, this, listen_fd_));

            return true;
        }

        // 启动主事件循环
        void Start()
        {
            main_loop_.Run();
        }

        // 停止主事件循环
        void Stop()
        {
            main_loop_.Stop();
        }

        // 主Reactor处理accept事件
        void OnAccept(int listen_fd)
        {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            // 检查连接数限制
            if (get_total_connections_ && get_total_connections_() >= static_cast<size_t>(max_connections_))
            {
                // 达到最大连接数，暂时停止 accept
                return;
            }

            while (true)
            {
                int client_fd = accept4(listen_fd_, (sockaddr *)&client_addr, &client_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (client_fd < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        break;
                    }
                    else
                    {
                        perror("accept");
                        continue;
                    }
                }

                // 设置TCP_NODELAY等选项
                SetSocketOptions(client_fd);

                // 分配给子Reactor处理
                SubReactor *target = load_balancer_.Select();
                if (target)
                {
                    target->AddConnection(client_fd, client_addr);
                }
                else
                {
                    // 没有可用的子Reactor，关闭连接
                    LOG_WARN << "no available sub-reactor, closing connection";
                    close(client_fd);
                }
            }
        }

    private:
        // 设置TCP选项
        void SetSocketOptions(int fd)
        {
            int yes = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
            setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

            // TCP Keep-Alive 参数：30秒空闲开始探测，5秒间隔，3次失败判定断开
            int keepidle = 30;
            int keepintvl = 5;
            int keepcnt = 3;
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
        }
    };

}