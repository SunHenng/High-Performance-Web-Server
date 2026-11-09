#pragma once

#include <assert.h>

#include <iostream>
#include <deque>
#include <vector>
#include <mutex>
#include <condition_variable>
using namespace std;

#ifndef MiltSync_HPP
#define MiltSync_HPP

    static const size_t MaxTaskCount = 500;
    template <class T>
    class SyncQueue
    {
    private:
        // std::deque<T> m_queue; // list
        std::vector<std::deque<T>> m_queue;
        size_t m_bucketSize; // 8
        size_t m_maxSize;    // 500
        mutable std::mutex m_mutex;
        std::condition_variable m_notEmpty; //
        std::condition_variable m_notFull;  //
        std::condition_variable m_waitStop;

        bool m_needStop; // true stop;
        int m_waitTime;  // ms;
        bool isFull(const int index) const
        {
            bool full = m_queue[index].size() >= m_maxSize;
            return full;
        }
        bool isEmpty(const int index) const
        {
            bool empty = m_queue[index].empty();
            return empty;
        }
        // 是否存在任意一个非空桶（须在持锁时调用）
        bool anyNonEmpty() const
        {
            for (size_t i = 0; i < m_bucketSize; ++i)
            {
                if (!m_queue[i].empty())
                {
                    return true;
                }
            }
            return false;
        }

        template <class F>
        int Add(F &&task, const int index)
        {
            if (index < 0 || static_cast<size_t>(index) >= m_bucketSize)
            {
                return -2;
            }
            // 生产者满时仍用 wait_for 兜底：这是罕见的过载回退路径，
            // 语义是「1ms 内仍满就返回 -1，由调用方内联执行」，不参与空闲轮询。
            std::unique_lock<std::mutex> locker(m_mutex);
            while (!m_needStop && isFull(index))
            {
                auto tag = m_notFull.wait_for(locker, std::chrono::milliseconds(m_waitTime));
                if (tag == std::cv_status::timeout)
                {
                    return -1;
                }
            }
            if (m_needStop)
            {
                return -2;
            }

            m_queue[index].emplace_back(std::forward<F>(task));
            m_notEmpty.notify_all();
            return 0;
        }

    public:
        SyncQueue(int bucketsize = 8, int maxsize = MaxTaskCount, int waitTime = 1)
            : m_bucketSize(bucketsize),
              m_maxSize(maxsize),
              m_waitTime(waitTime),
              m_needStop(false)
        {
            m_queue.resize(m_bucketSize);
        }
        ~SyncQueue()
        {
            if (!m_needStop)
            {
                Stop();
            }
        }
        int Put(const T &task, const int index)
        {
            return Add(task, index);
        }
        int Put(T &&task, const int index)
        {
            return Add(std::forward<T>(task), index);
        }

        // 非阻塞取整桶：仅当 index 桶非空时搬走。用于「先吃自己的、再窃取」的快路径。
        // 返回 0=取到，-1=空，-2=已停止。
        int TryTake(std::deque<T> *pdeq, const int index)
        {
            assert(pdeq != nullptr);
            if (index < 0 || static_cast<size_t>(index) >= m_bucketSize)
                return -2;
            std::unique_lock<std::mutex> locker(m_mutex);
            if (m_needStop)
            {
                return -2;
            }
            if (isEmpty(index))
            {
                return -1;
            }
            *pdeq = std::move(m_queue[index]);
            m_notFull.notify_all();
            m_waitStop.notify_all();
            return 0;
        }

        // 阻塞取整桶：直到「任意桶非空」或「停止」才返回。醒来后优先取 index 桶，
        // 否则窃取第一个非空桶，保留就近调度。返回 true=取到，false=已停止。
        bool TakeBlocking(std::deque<T> *pdeq, const int index)
        {
            assert(pdeq != nullptr);
            if (index < 0 || static_cast<size_t>(index) >= m_bucketSize)
                return false;
            std::unique_lock<std::mutex> locker(m_mutex);
            m_notEmpty.wait(locker, [this]()
                            { return m_needStop || anyNonEmpty(); });
            if (m_needStop)
            {
                return false;
            }
            if (!isEmpty(index))
            {
                *pdeq = std::move(m_queue[index]);
            }
            else
            {
                // 窃取第一个非空桶（谓词保证此刻 anyNonEmpty() 为真）
                for (size_t i = 0; i < m_bucketSize; ++i)
                {
                    if (!m_queue[i].empty())
                    {
                        *pdeq = std::move(m_queue[i]);
                        break;
                    }
                }
            }
            m_notFull.notify_all();
            m_waitStop.notify_all();
            return true;
        }

        int Take(T *ps, const int index)
        {
            assert(ps != nullptr);
            if (index < 0 || static_cast<size_t>(index) >= m_bucketSize)
                return -2;
            std::unique_lock<std::mutex> locker(m_mutex);

            while (!m_needStop && isEmpty(index))
            {
                auto tag = m_notEmpty.wait_for(locker, std::chrono::milliseconds(m_waitTime));
                if (std::cv_status::timeout == tag)
                {
                    return -1;
                }
            }
            if (m_needStop)
            {
                return -2;
            }
            *ps = m_queue[index].front();
            m_queue[index].pop_front();
            m_notFull.notify_all();
            m_waitStop.notify_all();
            return 0;
        }
        void Stop()
        {
            {
                std::unique_lock<std::mutex> locker(m_mutex);
                m_needStop = true;
            }
            m_notFull.notify_all();
            m_notEmpty.notify_all();
            m_waitStop.notify_all();
        }
        void WaitQueueEmptyStop()
        {
            std::unique_lock<std::mutex> locker(m_mutex);

            // 阻塞等待所有桶排空（消费者每次搬空一个桶都会 notify m_waitStop）。
            // 排空后置 m_needStop，唤醒仍阻塞在 TakeBlocking 上的 worker 退出。
            m_waitStop.wait(locker, [this]()
                            { return !anyNonEmpty(); });
            m_needStop = true;
            m_notFull.notify_all();
            m_notEmpty.notify_all();
        }
        bool Empty(const int index) const
        {
            std::unique_lock<std::mutex> locker(m_mutex);
            return m_queue[index].empty();
        }
        bool Full(const int index) const
        {
            std::unique_lock<std::mutex> locker(m_mutex);
            return m_queue[index].size() >= m_maxSize;
        }
        size_t Size(const int index) const
        {
            std::unique_lock<std::mutex> locker(m_mutex);
            return m_queue[index].size();
        }
        size_t Count(const int index) const
        {
            if (index < 0 || static_cast<size_t>(index) >= m_bucketSize)
                return 0;
            std::unique_lock<std::mutex> locker(m_mutex);
            return m_queue[index].size();
        }
        size_t TotalCount() const
        {
            std::unique_lock<std::mutex> locker(m_mutex);
            size_t total = 0;
            for (int i = 0; i < m_bucketSize; ++i)
            {
                total += m_queue[i].size();
            }
            return total;
        }
    };


#endif
