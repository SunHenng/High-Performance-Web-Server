#pragma once
#include <vector>
#include <unordered_map>
#include <functional>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <string.h> // for memset
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>

// (coroutine headers removed — unused)

namespace Reactors
{
    class EventLoop
    {
    public:
        using EventCallback = std::function<void(int)>;
        using TimerCallback = std::function<void()>;

        enum EventType
        {
            kNoneEvent = 0,
            kReadEvent = EPOLLIN,
            kWriteEvent = EPOLLOUT,
            kErrorEvent = EPOLLERR
        };

        EventLoop();
        ~EventLoop();

        // 禁止拷贝和移动
        EventLoop(const EventLoop &) = delete;
        EventLoop &operator=(const EventLoop &) = delete;
        EventLoop(EventLoop &&) = delete;
        EventLoop &operator=(EventLoop &&) = delete;

        // 运行事件循环
        void Run();

        // 停止事件循环
        void Stop();

        // 向事件循环中添加任务
        void QueueInLoop(const std::function<void()> &task);

        // 唤醒事件循环
        void WakeUp();
        int GetWakeupFd() const { return wakeup_fd_; }

        // 事件注册和注销
        void UpdateChannel(int fd, EventType events, const EventCallback &read_cb = nullptr, const EventCallback &write_cb = nullptr);
        void RemoveChannel(int fd);

        // 定时器接口
        // 添加周期性定时器，返回timer_id（可用于取消），interval_ms为间隔毫秒数
        int RunEvery(int interval_ms, TimerCallback cb);
        // 添加一次性定时器，delay_ms毫秒后执行一次
        int RunAfter(int delay_ms, TimerCallback cb);
        // 取消定时器
        void CancelTimer(int timer_id);

    private:
        // 处理唤醒事件
        void HandleWakeup();

        // 处理任务队列
        void DoPendingTasks();

        // 检查并触发到期定时器
        void CheckTimers();

        // 事件循环的核心函数
        void Loop();

    private:
        int epoll_fd_;              // epoll实例
        int wakeup_fd_;             // eventfd用于唤醒
        std::atomic<bool> looping_; // 事件循环是否在运行
        std::atomic<bool> quit_;    // 是否退出事件循环

        struct Channel
        {
            int fd;                       // 文件描述符
            EventType events;             // 关注的事件
            EventCallback read_callback;  // 读事件回调
            EventCallback write_callback; // 写事件回调
        };

        struct Timer
        {
            int id;                                                  // 定时器ID
            int interval_ms;                                         // 间隔毫秒数，-1表示一次性定时器
            std::chrono::steady_clock::time_point next_fire;         // 下次触发时间
            TimerCallback callback;                                  // 回调函数
        };

        std::unordered_map<int, Channel> channels_;        // fd到Channel的映射
        std::vector<epoll_event> active_events_;           // 活跃事件列表
        std::vector<std::function<void()>> pending_tasks_; // 待处理任务队列
        std::mutex mutex_;                                 // 保护待处理任务队列的互斥锁

        std::vector<Timer> timers_;                        // 定时器列表
        std::mutex timer_mutex_;                           // 保护定时器列表的互斥锁
        int next_timer_id_{0};                             // 下一个定时器ID
    };

}