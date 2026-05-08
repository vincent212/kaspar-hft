/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"
#include <sys/timeb.h>
#include "chutil/ut.hpp"
#include <set>
#include <tuple>
#include <boost/format.hpp>

#include "chutil/Time.hpp"

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time.hpp>
#include <iostream>

#include "logger/act/Logger.hpp"

using namespace std;

  static const char* get_name()  {
    return "Time";
  }

namespace chutil {


Time Time::now_utc()
{
  const boost::posix_time::ptime now_utc=
    boost::posix_time::microsec_clock::universal_time();
  auto date_part=now_utc.date();
  auto y=date_part.year();
  auto m=date_part.month();
  auto d=date_part.day();
  auto date_=y*10000+m*100+d;
  auto ns_ = (now_utc - boost::posix_time::ptime(date_part)).total_nanoseconds();

  return Time(int(date_), uint64_t(ns_));
}

Time Time::now_local()
{
  const boost::posix_time::ptime now_local=
    boost::posix_time::microsec_clock::local_time();
  auto date_part=now_local.date();
  auto y=date_part.year();
  auto m=date_part.month();
  auto d=date_part.day();
  auto date_=y*10000+m*100+d;
  auto ns_ = (now_local - boost::posix_time::ptime(date_part)).total_nanoseconds();
  return Time(int(date_), uint64_t(ns_));
}

Time Time::parse_with_us(const char* input, const char* fmt)
{
  struct tm tm;
  const char* us = ut_parse_date(input, fmt, &tm, false);
  ASSERT(us, "no us");
  us++;
  ASSERTF(strlen(us) == 6, boost::format("bad us %s too short") % us);
  uint64_t us_ = to_int(us);
  uint64_t ns_ = us_ * 1000;
  return Time(1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ns_);
}

Time Time::parse_with_ns(const char* input, const char* fmt)
{
  struct tm tm;
  const char* ns = ut_parse_date(input, fmt, &tm, false);
  ASSERT(ns, "no ns");
  ns++;
  ASSERTF(strlen(ns) == 9, boost::format("bad ns %s too short") % ns);
  uint64_t ns_ = to_int(ns);
  return Time(1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ns_);
}

 Time Time::parse(const char*input, const char*ns, const char*fmt)
  {
    struct tm tm;
    ut_parse_date(input,fmt,&tm);
    auto ns_ = boost::lexical_cast<uint64_t>(ns);
    Time tim(1900+tm.tm_year,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec, ns_);
    return tim;
  }

  Time Time::add_days(const Time& t, int days)
  {
    const auto& b = t.break_up();
    boost::gregorian::date d((unsigned short)get<0>(b), (unsigned short)get<1>(b), (unsigned short)get<2>(b));
    boost::gregorian::date_duration dd(days);
    d += dd;
    return Time(
      d.year(), d.month(), d.day(), 
      get<3>(b), get<4>(b), get<5>(b), get<8>(b));
  }

  Time Time::next_day(const Time& t)
  {
    return add_days(t, 1);
  }

  // rounds to ms and adds _ms
  // TODO: this is almost correct
  Time Time::operator+(int _ms) const
  {
    auto nms = ns / 1000 / 1000 + _ms;
    auto addDays = uint32_t(nms / _24_HOURS_ms);
    nms = nms % _24_HOURS_ms;
    if (addDays)
      return add_days(Time(date, uint64_t(nms) * 1000000), addDays);
    return Time(date, uint64_t(nms) * 1000000);
  }

  // year,month,day,hour,min,sec,ms
  tuple<uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint64_t,uint64_t>
  Time::break_up() const noexcept
  {
    tuple<uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint64_t,uint64_t> ret;
    uint32_t y,mon,d,h;
            
    y=date/10000;
    mon=(date-y*10000)/100;
    d=date-y*10000-mon*100;
    h=uint32_t( ns/3600ULL/1000ULL/1000ULL/1000ULL );

    uint64_t h2, m2, s2;
    uint64_t _ns;

    h2 = uint64_t(ns / 3600ULL / 1000ULL / 1000ULL / 1000ULL);
    m2 = uint64_t(ns - h2 * 3600ULL * 1000ULL * 1000ULL * 1000ULL) / 60ULL / 1000ULL / 1000ULL / 1000ULL;
    s2 = uint64_t(ns - (h2 * 3600ULL + m2 * 60ULL) * 1000ULL * 1000ULL * 1000ULL) / 1000ULL / 1000ULL / 1000ULL;
    _ns = ns - uint64_t(h2 * 3600ULL + m2 * 60ULL + s2) * 1000ULL * 1000ULL * 1000ULL;
    ASSERT(int(_ns) >= 0, "bad ns");
    auto __ns = (uint64_t(h2) * 3600ULL  + uint64_t(m2) * 60ULL + uint64_t(s2)) * 1000ULL * 1000ULL * 1000ULL + _ns;
    //ASSERT(__ns == ns, "bad __ns");
    if (__ns != ns)
    {
      log_err("bad __ns %d %d", __ns, ns);
    }

    get<0>(ret)=y;
    get<1>(ret)=mon;
    get<2>(ret)=d;
    get<3>(ret)=h;
    get<4>(ret)=uint32_t(m2);
    get<5>(ret)=uint32_t(s2);
    get<6>(ret)=uint32_t(_ns / 1000 / 1000);
    get<7>(ret)=_ns / 1000;
    get<8>(ret)=_ns;

    return ret;
  }

  string Time::to_string(int type) const
  {
    const auto&ret=break_up();
    switch (type)
      {
      case 0:
        {
          return (boost::format("%d%02d%02d-%02d:%02d:%02d.%09d")
                  % get<0>(ret)
                  % get<1>(ret)
                  % get<2>(ret)
                  % get<3>(ret)
                  % get<4>(ret)
                  % get<5>(ret)
                  % get<8>(ret)).str();
        }
      case 1:
        {
          return (boost::format("%02d/%02d/%04d %02d:%02d:%02d.%09d")
                  % get<1>(ret)
                  % get<2>(ret)
                  % get<0>(ret)
                  % get<3>(ret)
                  % get<4>(ret)
                  % get<5>(ret)
                  % get<8>(ret)).str();
        }
      case 2:
          return (boost::format("%d:%9d") % date % ns).str();
      case 3:
      {
          return (boost::format("%04d%02d%02d-%02d:%02d:%02d.%03d")
                  % get<0>(ret)
                  % get<1>(ret)
                  % get<2>(ret)
                  % get<3>(ret)
                  % get<4>(ret)
                  % get<5>(ret)
                  % get<6>(ret)).str();
      }

      default: {
        ERR("invalid code");
        return "";
      }
      }
  }

} // namespace chutil

