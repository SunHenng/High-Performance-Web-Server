
#include <iostream>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <vector>
using namespace std;
#include "log/LogFile.hpp"
#include "log/CountDownLatch.hpp"

#ifndef ASYN_LOGGING_HPP
#define ASYN_LOGGING_HPP
namespace logging
{
    class AsynLogging
    {
    private:
        void workthreadfunc();
        logging::CountDownLatch latch_;

    private:
        const std::string baseName_;
        const size_t rollSize_;   // 20M
        const int flushInterval_; // 3;
        std::atomic<bool> running_;
        std::unique_ptr<std::thread> pthread_; // new delete
        std::mutex mutex_;
        std::condition_variable cond_;

        std::string currentBuffer_; // 4k
        std::vector<std::string> buffers_;

        logging::LogFile output_; // rollSize 128k;

    public:
        AsynLogging(const std::string &basename,
                    const size_t rollsize = 1024 * 128,
                    int flushInterval = 3);
        ~AsynLogging();
        void append(const std::string &msg);
        void append(const char *msg, const size_t len);
        void start();
        void stop();
        void flush();/// output_->flush();
    };
} // namespace logging

#endif