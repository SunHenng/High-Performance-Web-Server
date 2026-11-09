

#include "log/AppendFile.hpp"
#include "log/Timestamp.hpp"

#include <time.h>

#include <string>
#include <memory>
#include <mutex>
using namespace std;

#ifndef LOGFILE_HPP
#define LOGFILE_HPP

namespace logging
{
    class LogFile
    {
    private:
        const std::string baseName_; // "yhping"
        const size_t rollSize_;      // 10M

        time_t startOfPeriod_; // 2025,10,13 20 21 30 => 2025 10 13 0:0:0
        time_t lastRoll_;
        time_t lastFlush_;
        static const size_t kRollPerSecods_ = 60 * 60 * 24;

        const int flushInterval_; // 3s;
        const int chechEventN_;   // 30
        int count_;               // 0

    private:
        std::unique_ptr<logging::AppendFile> file_; // new  delete; new delete;
        //logging::AppendFile file_;
        std::unique_ptr<std::mutex> mutex_;
        //std::mutex mutex_;

        static std::string getLogFileName(const std::string &basename, const logging::Timestamp &now);
        void append_unlocked(const char *msg, const size_t len);

    public:
        LogFile(const std::string &basename, size_t rollSize = 1024 * 4,
                int flushInterval = 3, int chechEventN = 30, bool threadSafe = true);
        ~LogFile();
        void append(const std::string &msg);             // C++
        void append(const char *data, const size_t len); // C
        void flush();
        bool rollFile();
    };
} // namespace logging

#endif