
#include <mutex>
#include <condition_variable>
using namespace std;

#ifndef COUNTDOWNLATCH_HPP
#define COUNTDOWNLATCH_HPP
namespace logging
{
    class CountDownLatch
    {
    private:
        int count_;
        std::mutex mutex_;
        std::condition_variable cond_;

    public:
        CountDownLatch(int x); // count_; // 2
        ~CountDownLatch() = default;

        void wait();      // count_ == 0;   wait;
        void countDown(); // count_ -=1;
        int getCount() const;
    };

} // namespace logging

#endif