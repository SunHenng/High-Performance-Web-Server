

// C++ STL;
#include <string>
// using namespace std;
//  C API
#include <stdint.h>

#ifndef TIMESTAMP_HPP
#define TIMESTAMP_HPP

namespace logging
{
    class Timestamp
    {
    private:
        //  1s = 1000ms;
        //  1s = 1000 * 1000 mms;
        //  1s = 1000 * 1000 * 1000 ns;
        uint64_t micr_; // 1970 1 1 0:0:0 --->
    public:
        Timestamp();
        Timestamp(uint64_t m);
        ~Timestamp();
        Timestamp(const Timestamp &) = default;
        Timestamp &operator=(const Timestamp &) = default;
        void swap(Timestamp &other);

        std::string toString() const;
        // 1720000000.1234;
        std::string toFormattedString(bool showmicro = true) const;
        // 2024/07/01 08:20:20.34567Z
        std::string toFormattedFile() const;
        // 20240701082020.34567z
        std::string toFormattedLog() const;

        bool valid() const; //
        uint64_t getMicro() const;
        uint64_t getMillis() const;
        uint64_t getSecond() const;
        const Timestamp &now();
        operator uint64_t() const;

    public:
        static Timestamp Now(); // not this; Timestamp::Now();
        static Timestamp Invalid();
        static const int kMinPerSec = 1000 * 1000;
    };

    inline time_t diffSecond(const Timestamp &a, const Timestamp &b)
    {
        return a.getSecond() - b.getSecond();
    }
    inline time_t diffMicro(const Timestamp &a, const Timestamp &b)
    {
        return a.getMicro() - b.getMicro();
    }
    inline Timestamp addTime(const Timestamp &timestamp, double seconds)
    {
        int64_t delta = static_cast<int64_t>(Timestamp::kMinPerSec * seconds);
        return Timestamp(timestamp.getMicro() + delta);
    }

    inline Timestamp subDays(const Timestamp &temstamp, const int days)
    {
        return Timestamp(temstamp.getMicro() - days * 24 * 60 * 60 * Timestamp::kMinPerSec);
    }
    inline Timestamp subHour(const Timestamp &temstamp, const int hour)
    {
        return Timestamp(temstamp.getMicro() - hour * 60 * 60 * Timestamp::kMinPerSec);
    }
    inline Timestamp subMinute(const Timestamp &temstamp, const int minute)
    {
        return Timestamp(temstamp.getMicro() - minute * 60 * Timestamp::kMinPerSec);
    }

}
#endif