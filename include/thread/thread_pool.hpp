#pragma once

#include "thread/milt_sync.hpp"

#include <functional>
#include <thread>
#include <mutex>
#include <vector>
#include <atomic>
#include <memory>
#include <iostream>
#include <future>
#include <concepts>
#include <type_traits>
using namespace std;

using TaskType = std::function<void(void)>;
class WorkStealingPool
{
private:
    SyncQueue<TaskType> m_queue;
    size_t m_numThreads;
    std::vector<std::jthread> m_threadgroup;
    std::stop_source m_stop_source;
    std::once_flag m_flag;
    std::atomic<size_t> next_index_{0};

    // 轮询桶号。必须是「每实例」的原子计数器——原来的函数局部 static 是进程级共享，
    // 且 num++ 无同步，多事件循环线程并发投递时对同一 size_t 未同步自增是数据竞争/UB。
    int threadIndex()
    {
        return static_cast<int>(next_index_.fetch_add(1, std::memory_order_relaxed) % m_numThreads);
    }

    void Start(int numthreads)
    {
        m_threadgroup.reserve(numthreads);
        for (int i = 0; i < numthreads; ++i)
        {
            m_threadgroup.emplace_back(&WorkStealingPool::RunInThread, this,
                                       m_stop_source.get_token(), i);
        }
    }

    void RunBatch(std::deque<TaskType> &taskque)
    {
        for (auto &task : taskque)
        {
            if (task)
            {
                task();
            }
        }
    }

    void RunInThread(std::stop_token stoken, const int index)
    {
        while (!stoken.stop_requested())
        {
            std::deque<TaskType> taskque;

            // 快路径：先吃自己桶（非阻塞）
            if (m_queue.TryTake(&taskque, index) == 0)
            {
                RunBatch(taskque);
            }
            else
            {
                // 窃取：轮询一个别人的桶（非阻塞）
                int other = threadIndex();
                if (other != index && m_queue.TryTake(&taskque, other) == 0)
                {
                    RunBatch(taskque);
                }
                else if (m_queue.TakeBlocking(&taskque, index))
                {
                    // 都空才阻塞等待；醒来后 own 优先、否则窃取任意非空桶
                    RunBatch(taskque);
                }
                else
                {
                    // TakeBlocking 返回 false = m_needStop，队列已排空，退出
                    break;
                }
            }
        }
    }

    void StopThreadGroup()
    {
        // ① 排空（WaitQueueEmptyStop 内部等所有桶排空后置 m_needStop）
        m_queue.WaitQueueEmptyStop();
        // ② 唤醒/停止所有 worker
        m_stop_source.request_stop();
        // ③ 等待 worker 退出（jthread::join）
        for (auto &t : m_threadgroup)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        m_threadgroup.clear();
    }

public:
    WorkStealingPool(const int queue = 200, const int numthread = 8)
        : m_queue(numthread, queue),
          m_numThreads(numthread)
    {
        Start(m_numThreads);
    }
    ~WorkStealingPool()
    {
        Stop();
    }
    void Stop()
    {
        std::call_once(m_flag, [this]() -> void
                       { StopThreadGroup(); });
    }

    void AddTask(TaskType &&task)
    {
        if (m_queue.Put(std::forward<TaskType>(task), threadIndex()) != 0)
        {
            std::cerr << "AddTask call task : " << endl;
            task();
        }
    }
    void AddTask(const TaskType &task)
    {
        if (m_queue.Put(task, threadIndex()) != 0)
        {
            std::cerr << "AddTask call task : " << endl;
            task();
        }
    }

    template <class Func, class... Args>
        requires std::invocable<Func &, Args...>
    void AddTask(Func &&func, Args &&...args)
    {
        auto task = std::make_shared<std::packaged_task<void()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        if (m_queue.Put([task]() -> void
                        { (*task)(); }, threadIndex()) != 0)
        {
            (*task)();
        }
    }

    template <class Func, class... Args>
        requires std::invocable<Func &, Args...>
    auto submit(Func &&func, Args &&...args)
    {
        using RetType = std::invoke_result_t<Func, Args...>;

        auto task = std::make_shared<std::packaged_task<RetType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        auto result = task->get_future();
        if (m_queue.Put([task]() -> void
                        { (*task)(); }, threadIndex()) != 0)
        {
            (*task)();
        }
        return result;
    }
};
