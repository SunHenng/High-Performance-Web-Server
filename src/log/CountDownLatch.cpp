
#include "log/CountDownLatch.hpp"

namespace logging
{
    // CountDownLatch
    // int count_;
    // std::mutex mutex_;
    // std::condition_variable cond_;

    CountDownLatch::CountDownLatch(int x)
        : count_(x)
    {
    }

    void CountDownLatch::wait()
    {
        std::unique_lock<std::mutex> locker(mutex_);
        while (count_ > 0)
        {
            cond_.wait(locker);
        }
    }
    void CountDownLatch::countDown()
    {
        std::unique_lock<std::mutex> locker(mutex_);
        count_ -= 1;
        if (count_ <= 0)
        {
            cond_.notify_all();
        }
    }
    int CountDownLatch::getCount() const
    {
        return count_;
    }

} // namespace logging