// C++ STL
#include<string>
#include<sstream>
using namespace std;

// own;
#include "log/LogCommon.hpp"

#ifndef LOGMESSAGE_HPP
#define LOGMESSAGE_HPP

namespace logging
{
    class LogMessage
    {
    private:
        std::string header_;
        std::string text_;
        logging::LOG_LEVEL level_;
    public:
        LogMessage(const logging::LOG_LEVEL &level,
                   const std::string &filename,
                   const std::string &funcname,
                   const int line);
        ~LogMessage();
        LogMessage(const LogMessage &) = default;
        LogMessage& operator=(const LogMessage &) = default;
        
        const logging::LOG_LEVEL &getLogLevel() const;
        const std::string toString() const;

        template<class _Ty>
        LogMessage & operator<<(const _Ty &text)
        {
            std::stringstream ss;
            ss << " : " << text;
            text_ += ss.str();
            return *this;
        }
    };
}
#endif