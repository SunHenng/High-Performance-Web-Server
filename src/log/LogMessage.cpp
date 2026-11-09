//owner
#include<thread>
using namespace std;

#include "log/LogMessage.hpp"
#include "log/Timestamp.hpp"
namespace logging
{
    // class LogMessage
    // std::string header_;
    // std::string text_;
    // logging::LOG_LEVEL level_;

    LogMessage::LogMessage(const logging::LOG_LEVEL &level,
                           const std::string &filename,
                           const std::string &funcname,
                           const int line)
        :header_{},text_{},level_(level)
    {
        std::stringstream ss;
        ss << logging::Timestamp::Now().toFormattedString()<<" ";
        ss << std::this_thread::get_id()<<" ";
        ss << logging::LLTOSTR[static_cast<int>(level_)] << " ";
        const size_t pos = filename.find_last_of('/');
        const std::string fname = filename.substr(pos + 1);
        ss << fname << " " << funcname << " " << line << " ";
        header_ = ss.str();
    }
    LogMessage ::~LogMessage()
    {
    }
    const logging::LOG_LEVEL & LogMessage::getLogLevel() const
    {
        return level_;
    }
    const std::string LogMessage::toString() const
    {
        return header_ + text_;
    }
}