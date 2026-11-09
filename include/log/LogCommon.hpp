
#ifndef LOGCOMMON_HPP
#define LOGCOMMON_HPP
namespace logging
{
    static const int SMALL_BUFF_LEN = 128;
    static const int MEDIAN_BUFF_LEN = 512;
    static const int LARGE_BUFF_LEN = 1024;
    enum class LOG_LEVEL
    {
        TRACE = 0,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL,
        NUM_LOG_LEVELS,
    };
    static const char *LLTOSTR[] = {
        "TRACE", // 0
        "DEBUG", // 1
        "INFO",  // 2
        "WARN",  // 3
        "ERROR", // 4
        "FATAL", // 5
        "NUM_LOG_LEVELS"};
}
#endif 