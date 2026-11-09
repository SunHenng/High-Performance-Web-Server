
#include "log/AsynLogging.hpp"

namespace logging
{
    static const size_t BufMaxLen = 1024 * 4; // 4k;
    static const size_t BufQueueSize = 16;
    // class AsynLogging
    void AsynLogging::workthreadfunc()
    {
        latch_.countDown();
        std::vector<std::string> buffersToWrite;
        while (running_)
        {
            {
                std::unique_lock<std::mutex> locker(mutex_);
                while (buffers_.empty() && running_)
                {
                    cond_.wait_for(locker, std::chrono::milliseconds(1000)); // 1) 2) 3) 4)
                }
                buffers_.push_back(std::move(currentBuffer_));
                currentBuffer_.reserve(BufMaxLen);
                buffersToWrite.swap(buffers_);
                buffers_.reserve(BufQueueSize); // 16 * 4k
            }
            if(buffersToWrite.size() > 25)
            {
                fprintf(stderr,"size: %ld \n",buffersToWrite.size());
                buffersToWrite.erase(buffersToWrite.begin()+2,buffersToWrite.end());
            }
            //fprintf(stderr,"workthreadfunc run ...\n");
            //fprintf(stderr,"size: %ld \n",buffersToWrite.size());
            //fprintf(stderr,"capa : %ld \n",buffersToWrite.capacity());

            for (auto &buf : buffersToWrite)
            {
                output_.append(buf);
            }
            buffersToWrite.clear();
        }
    }

    // const std::string baseName_;
    // const size_t rollSize_;   // 20M
    // const int flushInterval_; // 3;
    // std::atomic<bool> running_;
    // std::unique_ptr<std::thread> pthread_; // new delete
    // std::mutex mutex_;
    // std::condition_variable cond_;

    // std::string currentBuffer_; // 4k
    // std::vector<std::string> buffers_;

    // logging::LogFile output_; // rollSize 128k;

    AsynLogging::AsynLogging(const std::string &basename,
                             const size_t rollsize,
                             int flushInterval)
        : latch_(1),
          baseName_(basename),
          rollSize_(rollsize),
          flushInterval_(flushInterval),
          running_(false),
          pthread_(nullptr),
          mutex_{},
          cond_{},
          output_(basename, rollsize, flushInterval)
    {
        currentBuffer_.reserve(BufMaxLen);
        buffers_.reserve(BufQueueSize); // 16;
    }
    AsynLogging::~AsynLogging()
    {
        if (running_)
        {
            stop();
        }
    }
    void AsynLogging::append(const std::string &msg)
    {
        append(msg.c_str(), msg.size());
    }
    void AsynLogging::append(const char *msg, const size_t len)
    {
        std::unique_lock<std::mutex> locker(mutex_);
        if (currentBuffer_.size() >= BufMaxLen ||
            currentBuffer_.capacity() - currentBuffer_.size() < len)
        {
            buffers_.push_back(std::move(currentBuffer_));
            currentBuffer_.reserve(BufMaxLen);
        }

        currentBuffer_.append(msg, len);
        cond_.notify_all();
    }
    void AsynLogging::start()
    {
        running_ = true;
        pthread_.reset(new std::thread(&AsynLogging::workthreadfunc, this));
        latch_.wait();
    }
    void AsynLogging::stop()
    {
        running_ = false;
        cond_.notify_all();
        pthread_->join();
    }
    void AsynLogging::flush()
    {
       // fprintf(stderr," AsynLogging::flush() \n");
        std::vector<std::string> bufferToWrtier;
        std::unique_lock<std::mutex> locker(mutex_);
        buffers_.push_back(std::move(currentBuffer_));
        bufferToWrtier.swap(buffers_);
        for(auto &buf: bufferToWrtier)
        {
            output_.append(buf);
        }
        output_.flush();
        bufferToWrtier.clear();
        currentBuffer_.reserve(BufMaxLen);
        buffers_.reserve(BufQueueSize);
    }
} // namespace logging
