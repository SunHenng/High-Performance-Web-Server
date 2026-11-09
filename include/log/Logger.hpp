
// OWNER
#include "log/LogMessage.hpp"
// C++ STL
#include <string>
#include <functional>

#ifndef LOGGER_HPP
#define LOGGER_HPP

namespace logging
{
    class Logger
    {
    public:
        using OutputFun = std::function<void(const std::string &)>;
        using FlushFun = std::function<void(void)>;

    public:
        static OutputFun s_output_;
        static FlushFun s_flush_;
        static void SetOuput(OutputFun fun);
        static void SetFlush(FlushFun fun);

    private:
        logging::LogMessage impl_;

    public:
        Logger(const logging::LOG_LEVEL &level,
               const string &filename,
               const string &funcname,
               const int line);
        ~Logger();
        logging::LogMessage &stream();

    private:
        static logging::LOG_LEVEL s_level_;

    public:
        static logging::LOG_LEVEL getLogLevel();
        static void SetLogLevel(const LOG_LEVEL &level);
    };

    //-------------------------------------------

#define LOG_TRACE                                                \
    if (logging::Logger::getLogLevel() <= logging::LOG_LEVEL::TRACE) \
    logging::Logger(logging::LOG_LEVEL::TRACE, __FILE__, __func__, __LINE__).stream()

#define LOG_DEBUG                                                 \
if (logging::Logger::getLogLevel() <= logging::LOG_LEVEL::DEBUG) \
    logging::Logger(logging::LOG_LEVEL::DEBUG, __FILE__, __func__, __LINE__).stream()

#define LOG_INFO                                                 \
    if (logging::Logger::getLogLevel() <= logging::LOG_LEVEL::INFO) \
    logging::Logger(logging::LOG_LEVEL::INFO, __FILE__, __func__, __LINE__).stream()

#define LOG_WARN \
    logging::Logger(logging::LOG_LEVEL::WARN, __FILE__, __func__, __LINE__).stream()

#define LOG_ERROR \
    logging::Logger(logging::LOG_LEVEL::ERROR, __FILE__, __func__, __LINE__).stream()

#define LOG_FATAL \
    logging::Logger(logging::LOG_LEVEL::FATAL, __FILE__, __func__, __LINE__).stream()

#define LOG_SYSERR \
    logging::Logger(logging::LOG_LEVEL::ERROR, __FILE__, __func__, __LINE__).stream()

#define LOG_SYSFATAL \
    logging::Logger(logging::LOG_LEVEL::FATAL, __FILE__, __func__, __LINE__).stream()
}

#endif