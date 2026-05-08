#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <sys/timeb.h>
#include <time.h>
#include <set>
#include <tuple>
#include <string>
#include <boost/foreach.hpp>
#include <iostream>
#include <sstream>
#include <chrono>

#include "chutil/Macros.hpp"


template <class Duration>
using sys_time = std::chrono::time_point<std::chrono::system_clock, Duration>;
using sys_nanoseconds = sys_time<std::chrono::nanoseconds>;

namespace chutil
{
    constexpr uint64_t _24_HOURS_ms = 24*60*60*1000LL;
    constexpr uint64_t _24_HOURS_us = _24_HOURS_ms * 1000;
    constexpr uint64_t _24_HOURS_ns = _24_HOURS_us * 1000;

    class Time
    {

    public:

        uint32_t date; // current date
        uint64_t ns;   // ns since midnight
        uint64_t _epoch_ = 0; // will have value if converted using from_epoch

        uint32_t get_date() const {return date;}
        uint32_t get_ms() const { return uint32_t (ns / 1000 / 1000);}
        uint64_t get_us() const { return ns / 1000; }
        uint64_t get_ns() const {
          return ns;
        }
        uint32_t get_year() const { return date / 10000; }
        uint32_t get_month() const { return (date % 10000) / 100; }
        uint32_t get_day() const { return date % 100; }

        Time()
        {
          date = 0;
          ns = 0L;
        }
            
        static Time now_local();
        static Time now_utc();

        static
        sys_nanoseconds get_ts() { return std::chrono::system_clock::now(); }

        static
        uint64_t epoch() { return get_ts().time_since_epoch().count(); }

        Time(uint day, uint64_t _ns)
        {
          date = day;
          ns = _ns;
        }


        uint64_t to_epoch_utc(int offset = 0) const
        {
            //! make sure computer is UTC time zone for offset 0

            const auto& tb = break_up();
            struct tm t;
            memset(&t, 0, sizeof(tm)); // Initalize to all 0's
            t.tm_year = std::get<0>(tb)-1900; // This is year-1900, so 112 = 2012
            t.tm_mon = std::get<1>(tb)-1;
            t.tm_mday = std::get<2>(tb);
            t.tm_hour = std::get<3>(tb);
            t.tm_min = std::get<4>(tb);
            t.tm_sec = std::get<5>(tb);

            time_t time_since_epoch = mktime(&t) + offset * 3600;

            //truct tm *tm2  = gmtime(&time_since_epoch);
            //ASSERT(t.tm_hour == tm2->tm_hour, "bad offset");

            auto ret = uint64_t(time_since_epoch) * 1000LL * 1000LL * 1000LL;
            ret += std::get<8>(tb);
            return ret;
        }

        static
        Time from_epoch(uint64_t _ns)
        {
            uint64_t sec = _ns / 1000000000ULL;
            uint64_t ns = _ns - sec * 1000000000ULL;
            return from_epoch(static_cast<long>(sec), static_cast<long>(ns), _ns);
        }

        static
        Time from_epoch(long sec_since_epoch, long ns, uint64_t _epoch_)
        {
            struct tm tm_buf;
            gmtime_r(&sec_since_epoch, &tm_buf);
            const struct tm* tm = &tm_buf;
            Time t(1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, static_cast<uint64_t>(ns));
            t._epoch_ = _epoch_;
            return t;
        }

        Time(uint year, uint mon, uint day, uint h, uint m, uint s=0, uint64_t _ns = 0)
        {
            date=year*10000+mon*100+day;
            ns = uint64_t(h * 3600 + m * 60 + s) * 1000 * 1000 * 1000 + _ns;
        }

        static
        Time parse_with_us(const char* input, const char* fmt);

        static
        Time parse_with_ns(const char* input, const char* fmt);

        static
        Time parse(const char*input, const char*ns, const char*fmt);

        bool operator==(const Time&t) const
        {
            return t.date==date&&t.ns==ns;
        }

        bool operator<=(const Time&t) const
        {
            return *this==t || *this<t;
        }

        bool operator>=(const Time&t) const
        {
            return *this==t || *this>t;
        }

        bool operator<(const Time&t) const
        {
          if (date < t.date)
            return true;
          else if (date > t.date)
            return false;
          else
            return ns < t.ns;
        }

        bool operator>(const Time&t) const
        {
            return !(*this<=t);
        }

        Time operator+(int _ms) const; // legacy

        Time add_micros(int64_t _us) const {
            uint64_t ns2 = ns + uint64_t(_us * 1000);
            if (ns2 > _24_HOURS_ns)
            {
                return add_days(Time(date,ns2 % _24_HOURS_ns),1);
            }
            else {
                return Time(date,ns2);
            }
        }

        int64_t operator-(const Time&t2) const
        {
            int64_t ns_diff = int64_t(ns) - int64_t(t2.ns);
            if (date == t2.date)
            {
                return ns_diff;
            }
            int64_t date_diff = int(date) - int(t2.date);
            return date_diff * _24_HOURS_ns + ns_diff;
        }

#ifdef TIM_MATH
        int days_diff(const Time&t) const;
        int operator-(const Time&t) const;

        Time operator-(int _ms) const
        {
            return *this+(-_ms);
        }

        Time&operator+=(int _ms)
        {
            *this=*this+_ms;
            return *this;
        }

        Time&operator-=(int _ms)
        {
            *this=*this-_ms;
            return *this;
        }
#endif

        bool not_zero() const
        {
            return date!=0L;
        }

        bool is_zero() const
        {
            return date==0L;
        }

        void set_zero()
        {
            date = 0;
            ns = 0;
        }

        bool is_valid() const
        {
            return (date>0L && date > 20200000);
        }

        int hour() const
        {
            const auto&b=break_up();
            return std::get<3>(b);
        }

        int minute() const
        {
            const auto&b=break_up();
            return std::get<4>(b);
        }

        int second() const
        {
            const auto&b=break_up();
            return std::get<5>(b);
        }

        // year,month,day,hour,min,sec,ms
        std::tuple<uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint64_t,uint64_t>
        break_up() const noexcept;

        static Time next_day(const Time&t);
        static Time add_days(const Time&t,int days);


        int get_hour() const
        {
            const auto&ret=break_up();
            return std::get<3>(ret);
        }

        int get_minute() const
        {
          const auto& ret = break_up();
          return std::get<4>(ret);
        }

        std::tuple<int,int>
        get_hour_minute() const
        {
            const auto&ret=break_up();
            return std::tuple<int,int>(std::get<3>(ret),std::get<4>(ret));
        }

        auto get_hour_minute_sec() const
        {
            const auto&ret=break_up();
            return std::make_tuple(std::get<3>(ret),std::get<4>(ret),std::get<5>(ret));
        }

        std::string to_string(int type=0) const;

        friend std::ostream& operator<<(std::ostream& out,const Time&t) 
        {
            return out << t.to_string();
        }


        

    };


}
