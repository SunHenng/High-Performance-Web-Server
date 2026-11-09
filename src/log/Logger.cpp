
#include<stdio.h>
#include "log/Logger.hpp"

namespace logging
{
    void defaultOutput(const std::string &msg)
    {
        size_t n = fwrite(msg.c_str(), sizeof(char), msg.size(), stdout);
    }
    void defaultFlush()
    {
        fflush(stdout);
    }
    logging::LOG_LEVEL InitLogLevel()
    {
        if (::getenv("logging::LOG_TRACE"))
        {
            return logging::LOG_LEVEL::TRACE;
        }else if(::getenv("logging::LOG_DEBUG"))
        {
            return logging::LOG_LEVEL::DEBUG;
        }else
        {
            return logging::LOG_LEVEL::INFO;
        }
    }
    // class Logger
    // using OutputFun = std::function<void(const std::string &)>;
    // using FlushFun = std::function<void(void)>;
    Logger::OutputFun Logger::s_output_ = defaultOutput;
    Logger::FlushFun Logger::s_flush_ = defaultFlush;
    void Logger::SetOuput(OutputFun out)
    {
        s_output_ = out;
    }
    void Logger::SetFlush(FlushFun flush)
    {
        s_flush_ = flush;
    }

    // logging::LogMessage impl_;

    Logger::Logger(const logging::LOG_LEVEL &level,
                   const string &filename,
                   const string &funcname,
                   const int line)
        : impl_(level, filename, funcname, line)
    {
    }
    Logger::~Logger()
    {
        impl_ << "\n";
        s_output_(impl_.toString());
        
        s_flush_();
        if(impl_.getLogLevel() == LOG_LEVEL::FATAL)
        {
            fprintf(stderr, "Process exit \n");
            exit(EXIT_FAILURE);
        }
    }
    logging::LogMessage &Logger::stream() { return impl_; }

    logging::LOG_LEVEL Logger::s_level_ = InitLogLevel();

    logging::LOG_LEVEL Logger::getLogLevel()
    {
        return s_level_;
    }
    void Logger::SetLogLevel(const LOG_LEVEL &level)
    {
        s_level_ = level;
    }
 
}