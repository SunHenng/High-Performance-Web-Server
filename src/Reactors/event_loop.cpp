#include "Reactors/event_loop.hpp"
#include <iostream>
#include <cerrno> // EINTR
#include "log/Logger.hpp"

namespace Reactors
{
    EventLoop::EventLoop()
        : epoll_fd_(epoll_create1(EPOLL_CLOEXEC)),
          wakeup_fd_(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
          looping_(false),
          quit_(false),
          active_events_(16)
    {
        UpdateChannel(wakeup_fd_, kReadEvent, [this](int)
                      { HandleWakeup(); });
    }

    EventLoop::~EventLoop()
    {
        close(epoll_fd_);
        close(wakeup_fd_);
    }

    void EventLoop::Run()
    {
        looping_ = true;
        quit_ = false;
        Loop();
        looping_ = false;
    }

    void EventLoop::Stop()
    {
        quit_ = true;
        if (looping_)
        {
            WakeUp();
        }
    }

    void EventLoop::QueueInLoop(const std::function<void()> &task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_tasks_.push_back(task);
        }
        WakeUp();
    }

    void EventLoop::WakeUp()
    {
        uint64_t one = 1;
        // eventfd 写是 8 字节原子操作，失败面很窄：EINTR（信号打断，重写一次即可）
        // 或 EAGAIN（计数器溢出到 2^64-1，现实不可能）。显式消费返回值，
        // 顺带消掉 glibc 的 -Wunused-result。
        ssize_t n;
        do
        {
            n = write(wakeup_fd_, &one, sizeof(one));
        } while (n < 0 && errno == EINTR);
    }

    // ==================== 定时器接口 ====================

    int EventLoop::RunEvery(int interval_ms, TimerCallback cb)
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        int id = next_timer_id_++;
        auto now = std::chrono::steady_clock::now();
        timers_.push_back({id, interval_ms,
                           now + std::chrono::milliseconds(interval_ms),
                           std::move(cb)});
        // 唤醒事件循环以重新计算epoll_wait超时时间
        WakeUp();
        return id;
    }

    int EventLoop::RunAfter(int delay_ms, TimerCallback cb)
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        int id = next_timer_id_++;
        auto now = std::chrono::steady_clock::now();
        timers_.push_back({id, -1, // -1 表示一次性定时器
                           now + std::chrono::milliseconds(delay_ms),
                           std::move(cb)});
        WakeUp();
        return id;
    }

    void EventLoop::CancelTimer(int timer_id)
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        // C++20 std::erase_if：按谓词删除匹配项。timer id 由 next_timer_id_ 单调分配
        // 不重复，等价于原 find_if + erase（最多删一个）。
        std::erase_if(timers_, [timer_id](const Timer &t)
                      { return t.id == timer_id; });
    }

    // 检查并触发到期定时器
    void EventLoop::CheckTimers()
    {
        auto now = std::chrono::steady_clock::now();
        std::vector<TimerCallback> fired_callbacks;

        {
            std::lock_guard<std::mutex> lock(timer_mutex_);

            auto it = timers_.begin();
            while (it != timers_.end())
            {
                if (it->next_fire <= now)
                {
                    fired_callbacks.push_back(std::move(it->callback));

                    if (it->interval_ms > 0)
                    {
                        // 周期性定时器：更新下次触发时间
                        it->next_fire = now + std::chrono::milliseconds(it->interval_ms);
                        ++it;
                    }
                    else
                    {
                        // 一次性定时器：移除
                        it = timers_.erase(it);
                    }
                }
                else
                {
                    ++it;
                }
            }
        }

        // 在锁外执行回调，避免死锁
        for (auto &cb : fired_callbacks)
        {
            if (cb)
            {
                cb();
            }
        }
    }

    // 事件注册
    void EventLoop::UpdateChannel(int fd, EventType events, const EventCallback &read_cb, const EventCallback &write_cb)
    {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.data.fd = fd;
        ev.events = static_cast<uint32_t>(events);

        auto it = channels_.find(fd);
        if (it == channels_.end())
        {
            channels_[fd] = {fd, events, read_cb, write_cb};
            epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
        }
        else
        {
            it->second.events = events;
            it->second.read_callback = read_cb;
            it->second.write_callback = write_cb;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
        }
    }

    // 事件注销
    void EventLoop::RemoveChannel(int fd)
    {
        auto it = channels_.find(fd);
        if (it != channels_.end())
        {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
            channels_.erase(it);
        }
    }

    // 处理唤醒事件
    void EventLoop::HandleWakeup()
    {
        uint64_t one = 0;
        // 读空（EAGAIN）是正常路径：epoll 报可读之后计数可能已被别的分支取走，
        // 直接返回即可。EINTR 则重读一次。显式消费返回值，消掉 -Wunused-result。
        ssize_t n;
        do
        {
            n = read(wakeup_fd_, &one, sizeof(one));
        } while (n < 0 && errno == EINTR);
    }

    // 处理挂起任务
    void EventLoop::DoPendingTasks()
    {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks.swap(pending_tasks_);
        }

        for (const auto &task : tasks)
        {
            task();
        }
    }

    // 事件循环 核心函数
    void EventLoop::Loop()
    {
        while (!quit_)
        {
            // 计算epoll_wait超时时间：取最近的定时器触发时间与默认100ms的最小值
            int timeout_ms = 100; // 默认100ms
            {
                std::lock_guard<std::mutex> lock(timer_mutex_);
                if (!timers_.empty())
                {
                    auto now = std::chrono::steady_clock::now();
                    for (const auto &t : timers_)
                    {
                        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        t.next_fire - now)
                                        .count();
                        if (diff < timeout_ms)
                        {
                            timeout_ms = diff > 0 ? static_cast<int>(diff) : 0;
                        }
                    }
                }
            }

            int num_events = epoll_wait(epoll_fd_, active_events_.data(),
                                        static_cast<int>(active_events_.size()), timeout_ms);

            if (num_events > 0)
            {
                // 处理活跃事件
                for (int i = 0; i < num_events; ++i)
                {
                    int fd = active_events_[i].data.fd;
                    uint32_t revents = active_events_[i].events;

                    // 每次调用回调前都重新查找 channels_，不要跨回调复用同一个迭代器。
                    // 原因：回调最终会走到 CloseConnection() → RemoveChannel() 里的
                    // channels_.erase()，那会让指向该节点的所有迭代器失效。
                    // 复用旧的 it 去解引用，就是 use-after-free——被释放的节点内存里
                    // 可能已经被 glibc 写入了 tcache 链表指针，读出来的 std::function
                    // 是一堆垃圾，调用它直接跳到野地址（压测收尾时必现段错误）。
                    // 重新查找只是一次哈希查询，不分配内存，开销可忽略。
                    if (revents & EPOLLIN)
                    {
                        auto it = channels_.find(fd);
                        if (it != channels_.end() && it->second.read_callback)
                        {
                            it->second.read_callback(fd);
                        }
                    }
                    if (revents & EPOLLOUT)
                    {
                        auto it = channels_.find(fd);
                        if (it != channels_.end() && it->second.write_callback)
                        {
                            it->second.write_callback(fd);
                        }
                    }
                    if (revents & EPOLLERR)
                    {
                        auto it = channels_.find(fd);
                        if (it != channels_.end())
                        {
                            if (it->second.read_callback)
                                it->second.read_callback(fd);
                            else if (it->second.write_callback)
                                it->second.write_callback(fd);
                        }
                    }
                }

                // 扩容
                if (static_cast<size_t>(num_events) == active_events_.size())
                {
                    active_events_.resize(active_events_.size() * 2);
                }
            }
            else if (num_events == -1)
            {
                // 处理错误
                continue;
            }

            // 处理待处理任务
            DoPendingTasks();

            // 检查并触发定时器
            CheckTimers();
        }
    }

}