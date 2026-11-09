#pragma once
#include <vector>
#include <mutex>
#include "Reactors/SubReactor.hpp"

namespace Reactors
{
    class LoadBalancer
    {
    public:
        enum Strategy
        {
            ROUND_ROBIN,       // 轮询策略
            LEAST_CONNECTIONS, // 最少连接策略
            RANDOM             // 随机策略
        };

        LoadBalancer(int thread_num = 0, Strategy strategy = ROUND_ROBIN)
            : strategy_(strategy),
              sub_reactors_(),
              next_index_(0),
              mutex_()
        {
            sub_reactors_.reserve(thread_num);
        }

        // 添加子Reactor
        void AddSubReactor(SubReactor *sub_reactor)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sub_reactors_.push_back(sub_reactor);
        }

        // 选择一个子Reactor来处理新连接
        SubReactor *Select()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (sub_reactors_.empty())
            {
                return nullptr;
            }

            switch (strategy_)
            {
            case ROUND_ROBIN:
                return SelectRoundRobin();
            case LEAST_CONNECTIONS:
                return SelectLeastConnections();
            case RANDOM:
                return SelectRandom();
            default:
                return SelectRoundRobin();
            }
        }

        // 获取子Reactor数量
        size_t Size() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return sub_reactors_.size();
        }

    private:
        // 轮询选择
        SubReactor *SelectRoundRobin()
        {
            if (sub_reactors_.empty())
            {
                return nullptr;
            }
            SubReactor *selected = sub_reactors_[next_index_];
            next_index_ = (next_index_ + 1) % sub_reactors_.size();
            return selected;
        }

        // 最少连接选择
        SubReactor *SelectLeastConnections()
        {
            if (sub_reactors_.empty())
            {
                return nullptr;
            }
            SubReactor *selected = sub_reactors_[0];
            size_t min_connections = selected->GetConnectionCount();

            for (auto sub_reactor : sub_reactors_)
            {
                size_t connections = sub_reactor->GetConnectionCount();
                if (connections < min_connections)
                {
                    min_connections = connections;
                    selected = sub_reactor;
                }
            }
            return selected;
        }

        // 随机选择
        SubReactor *SelectRandom()
        {
            if (sub_reactors_.empty())
            {
                return nullptr;
            }
            size_t index = rand() % sub_reactors_.size();
            return sub_reactors_[index];
        }

    private:
        Strategy strategy_;                      // 负载均衡策略
        std::vector<SubReactor *> sub_reactors_; // 子Reactor列表
        size_t next_index_;                      // 轮询索引
        mutable std::mutex mutex_;               // 保护共享数据的互斥锁
    };
}