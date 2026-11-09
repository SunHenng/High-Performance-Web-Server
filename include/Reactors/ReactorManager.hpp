#pragma once
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include "Reactors/MainReactor.hpp"
#include "Reactors/SubReactor.hpp"
#include "Reactors/LoadBalancer.hpp"

#include <iostream> // for std::cerr
#include <stdexcept> // for std::runtime_error
#include <thread>    // for std::this_thread::sleep_for
#include <chrono>    // for std::chrono::milliseconds
#include "log/Logger.hpp"

namespace Reactors
{
    class ReactorManager
    {
    private:
        int sub_reactor_count_;                                 // 子Reactor数量
        std::unique_ptr<MainReactor> main_reactor_;             // 主Reactor
        LoadBalancer load_balancer_;                            // 负载均衡器
        std::vector<std::unique_ptr<SubReactor>> sub_reactors_; // 子Reactor列表
        std::vector<std::thread> sub_reactor_threads_;          // 子Reactor线程
        std::atomic<bool> running_;                             // 是否正在运行
        std::string root_dir_;                                  // 根目录
        int idle_timeout_seconds_{30};                          // 空闲连接超时秒数
        int max_connections_{10000};                            // 最大并发连接数
    public:
        ReactorManager(int sub_reactor_count = std::thread::hardware_concurrency(),
                       const std::string &root_dir = "./static/",
                       int idle_timeout_seconds = 30,
                       int max_connections = 10000)
            : sub_reactor_count_(sub_reactor_count),
              main_reactor_(nullptr),
              load_balancer_(sub_reactor_count, LoadBalancer::ROUND_ROBIN),
              sub_reactors_(),
              sub_reactor_threads_(),
              running_(false),
              root_dir_(root_dir),
              idle_timeout_seconds_(idle_timeout_seconds),
              max_connections_(max_connections)
        {
        }

        ~ReactorManager()
        {
            Stop();
        }

        // 初始化所有Reactor
        void Initialize()
        {
            // 创建子Reactor
            for (int i = 0; i < sub_reactor_count_; ++i)
            {
                auto sub_reactor = std::make_unique<SubReactor>(root_dir_, idle_timeout_seconds_);
                sub_reactors_.push_back(std::move(sub_reactor));
                load_balancer_.AddSubReactor(sub_reactors_.back().get());
            }

            // 创建主Reactor，传入连接数限制和计数回调
            main_reactor_ = std::make_unique<MainReactor>(
                load_balancer_,
                max_connections_,
                [this]() -> size_t
                {
                    size_t total = 0;
                    for (auto &sr : sub_reactors_)
                    {
                        total += sr->GetConnectionCount();
                    }
                    return total;
                });
        }

        // 启动所有Reactor
        void Start()
        {
            if (running_.exchange(true))
            {
                return; // 已经在运行
            }

            // 启动子Reactor线程
            for (auto &sub_reactor : sub_reactors_)
            {
                // 使用指针捕获而非引用捕获，避免悬空引用
                auto sub_reactor_ptr = sub_reactor.get();
                sub_reactor_threads_.emplace_back([sub_reactor_ptr]()
                                                  { sub_reactor_ptr->Start(); });
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // 启动主Reactor（在当前线程运行）
            main_reactor_->Start();
        }

        // 停止所有Reactor
        void Stop()
        {
            if (!running_.exchange(false))
            {
                return; // 已经停止
            }

            // 停止主Reactor
            if (main_reactor_)
            {
                main_reactor_->Stop();
            }

            // 停止子Reactor
            for (auto &sub_reactor : sub_reactors_)
            {
                sub_reactor->Stop();
            }

            // 等待子Reactor线程结束
            for (auto &thread : sub_reactor_threads_)
            {
                if (thread.joinable())
                {
                    thread.join();
                }
            }

            // 清空资源
            sub_reactor_threads_.clear();
            sub_reactors_.clear();
            main_reactor_.reset();
        }

        // 获取负载均衡器
        LoadBalancer &GetLoadBalancer()
        {
            return load_balancer_;
        }

        // 获取主Reactor
        MainReactor &GetMainReactor()
        {
            if (!main_reactor_)
            {
                throw std::runtime_error("MainReactor not initialized");
            }
            return *main_reactor_;
        }

        // 获取子Reactor列表
        const std::vector<std::unique_ptr<SubReactor>> &GetSubReactors() const
        {
            return sub_reactors_;
        }

        // 获取子Reactor数量
        size_t GetSubReactorCount() const
        {
            return sub_reactor_count_;
        }
        
    };
}