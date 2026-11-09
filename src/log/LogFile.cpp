
#include "log/Timestamp.hpp"
#include "log/LogCommon.hpp"

#include "log/LogFile.hpp"

#include <unistd.h>

#include <iostream>
using namespace std;

namespace logging
{
    // class LogFile
    //  const std::string baseName_; // "yhping"
    // const size_t rollSize_;      // 10M
    // time_t startOfPeriod_; // 2025,10,13 20 21 30 => 2025 10 13 0:0:0
    // time_t lastRoll_;
    // time_t lastFlush_;
    // static const size_t kRollPerSecods_ = 60 * 60 * 24;
    // const int flushInterval_; // 3s;
    // const int chechEventN_;   // 30
    // int count_;               // 0
    // std::unique_ptr<logging::AppendFile> file_; // new  delete; new delete;
    // std::unique_ptr<std::mutex> mutex_;

    const std::string hostname()
    {
        char buff[SMALL_BUFF_LEN] = {};
        if (!::gethostname(buff, SMALL_BUFF_LEN))
        {
            return std::string(buff);
        }
        else
        {
            return std::string("unknownhost");
        }
    }
    pid_t pid()
    {
        return ::getpid();
    }
    std::string LogFile::getLogFileName(const std::string &basename, const logging::Timestamp &now)
    {
        std::string filename = basename; // filename = "yhping";
        filename += ".";
        filename += now.toFormattedFile(); // "yhping20251013..."
        filename += ".";
        filename += hostname();
        filename += ".";
        filename += ::to_string(pid());
        filename += ".log";
        return filename;
    }
    void LogFile::append_unlocked(const char *msg, const size_t len)
    {
        file_->append(msg, len);
        if (file_->getWriteBytes() >= rollSize_)
        {
            rollFile();
        }
        else
        {
            count_++;
            if (count_ > chechEventN_)
            {
                count_ = 0;
                time_t now = time(nullptr);
                time_t thisPeriod = (now / kRollPerSecods_) * kRollPerSecods_;
                if (thisPeriod != startOfPeriod_)
                {
                    rollFile();
                }
                else if (now - lastFlush_ > flushInterval_)
                {
                    lastFlush_ = now;
                    file_->flush();
                }
            }
        }
    }

    LogFile::LogFile(const std::string &baseName, size_t rollSize,
                     int flushInterval, int chechEventN, bool threadSafe)
        : baseName_(baseName),
          rollSize_(rollSize),
          flushInterval_(flushInterval),
          chechEventN_(chechEventN),
          count_(0),
          startOfPeriod_(0),
          lastRoll_(0),
          lastFlush_(0),
          mutex_(threadSafe ? new std::mutex{} : nullptr)

    {
        rollFile();
    }
    LogFile::~LogFile()
    {
    }
    void LogFile::append(const std::string &msg)
    {
        append(msg.c_str(), msg.size());
    } // C++
    void LogFile::append(const char *data, const size_t len)
    {
        if (mutex_) // operator bool() const
        {
            std::unique_lock<std::mutex> locker(*mutex_);
            append_unlocked(data, len);
        }
        else
        {
            append_unlocked(data, len);
        }
    }
    void LogFile::flush()
    {
        file_->flush();
    }
    bool LogFile::rollFile()
    {
        logging::Timestamp now;
        now.now();
        std::string filename = getLogFileName(baseName_, now);
        time_t start = (now.getSecond() / kRollPerSecods_) * kRollPerSecods_;
        if (now.getSecond() > lastRoll_) // 0
        {
            lastRoll_ = now.getSecond();
            lastFlush_ = now.getSecond();
            startOfPeriod_ = start;
            file_.reset(new AppendFile(filename));
            //
            return true;
        }
        return false;
    }

} // namespace logging
