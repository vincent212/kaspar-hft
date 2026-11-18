/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "clearstreet_om/CsFix.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "quickfix/Message.h"

namespace clearstreet::om
{
  namespace
  {
    FIX::Message app(const char *type)
    {
      FIX::Message m;
      m.getHeader().setField(8, "FIX.4.2");
      m.getHeader().setField(35, type);
      return m;
    }
    void put(FIX::Message &m, int tag, const std::string &v)
    {
      if (!v.empty())
        m.setField(tag, v);
    }
    void put(FIX::Message &m, int tag, char v)
    {
      if (v)
        m.setField(tag, std::string(1, v));
    }

    // The NewOrderSingle body, shared by 35=D and 35=G ("its body is the
    // NewOrderSingle body plus OrigClOrdID (41)").
    void order_body(FIX::Message &m, const NewOrderReq &r)
    {
      put(m, 1, r.account);
      put(m, 11, r.clordid);
      m.setField(21, "1"); // HandlInst: must be 1
      if (r.extended_hours)
        m.setField(336, "6");
      put(m, 55, r.symbol);
      put(m, 54, r.side);
      put(m, 77, r.open_close);
      put(m, 60, r.transact_time.empty() ? utc_now() : r.transact_time);
      put(m, 38, r.qty);
      put(m, 40, r.ord_type);
      put(m, 44, r.px);
      put(m, 99, r.stop_px);
      put(m, 59, r.tif);
      if (r.target_strategy)
        m.setField(847, std::to_string(r.target_strategy));
      put(m, 100, r.ex_destination);
    }

    bool get(const FIX::FieldMap &m, int tag, std::string &out)
    {
      if (!m.isSetField(tag))
        return false;
      out = m.getField(tag);
      return true;
    }
    std::string opt(const FIX::FieldMap &m, int tag)
    {
      std::string s;
      get(m, tag, s);
      return s;
    }
    char opt_char(const FIX::FieldMap &m, int tag)
    {
      auto s = opt(m, tag);
      return s.empty() ? 0 : s[0];
    }
    double opt_num(const FIX::FieldMap &m, int tag)
    {
      auto s = opt(m, tag);
      return s.empty() ? 0.0 : std::strtod(s.c_str(), nullptr);
    }
    int opt_int(const FIX::FieldMap &m, int tag)
    {
      auto s = opt(m, tag);
      return s.empty() ? -1 : std::atoi(s.c_str());
    }
    bool need(const FIX::FieldMap &m, int tag, std::string &err)
    {
      if (m.isSetField(tag))
        return true;
      err = "required tag " + std::to_string(tag) + " missing";
      return false;
    }
    void header(const FIX::Message &m, uint64_t &seq, bool &dup)
    {
      seq = uint64_t(std::strtoull(opt(m.getHeader(), 34).c_str(), nullptr, 10));
      dup = opt(m.getHeader(), 43) == "Y";
    }
  }

  FIX::Message build_new_order(const NewOrderReq &r)
  {
    auto m = app("D");
    order_body(m, r);
    return m;
  }

  FIX::Message build_replace(const ReplaceReq &r)
  {
    auto m = app("G");
    order_body(m, r);
    put(m, 41, r.orig_clordid);
    return m;
  }

  FIX::Message build_cancel(const CancelReq &r)
  {
    auto m = app("F");
    put(m, 1, r.account);
    put(m, 41, r.orig_clordid);
    put(m, 11, r.clordid);
    put(m, 55, r.symbol);
    put(m, 54, r.side);
    put(m, 60, r.transact_time.empty() ? utc_now() : r.transact_time);
    put(m, 38, r.qty);
    return m;
  }

  bool parse_exec(const FIX::Message &m, ExecReport &r, std::string &err)
  {
    try
    {
      for (int tag : {37, 17, 150, 39, 11, 54, 151, 14})
        if (!need(m, tag, err))
          return false;
      r.order_id = opt(m, 37);
      r.exec_id = opt(m, 17);
      r.exec_trans_type = m.isSetField(20) ? opt_char(m, 20) : '0';
      r.exec_type = opt_char(m, 150);
      r.ord_status = opt_char(m, 39);
      r.clordid = opt(m, 11);
      r.orig_clordid = opt(m, 41);
      r.account = opt(m, 1);
      r.symbol = opt(m, 55);
      r.side = opt_char(m, 54);
      r.qty = opt_num(m, 38);
      r.px = opt_num(m, 44);
      r.stop_px = opt_num(m, 99);
      r.last_px = opt_num(m, 31);
      r.last_qty = opt_num(m, 32);
      r.leaves = opt_num(m, 151);
      r.cum = opt_num(m, 14);
      r.avg_px = opt_num(m, 6);
      r.rej_reason = opt_int(m, 103);
      r.restatement = opt(m, 378);
      r.transact_time = opt(m, 60);
      r.effective_time = opt(m, 168);
      r.text = opt(m, 58);
      header(m, r.seq, r.poss_dup);
      return true;
    }
    catch (const std::exception &e)
    {
      err = e.what();
      return false;
    }
  }

  bool parse_cancel_reject(const FIX::Message &m, CancelReject &r, std::string &err)
  {
    try
    {
      for (int tag : {11, 41, 39, 434})
        if (!need(m, tag, err))
          return false;
      r.order_id = opt(m, 37);
      r.clordid = opt(m, 11);
      r.orig_clordid = opt(m, 41);
      r.ord_status = opt_char(m, 39);
      r.account = opt(m, 1);
      r.response_to = opt_char(m, 434);
      r.reason = opt_int(m, 102);
      r.text = opt(m, 58);
      header(m, r.seq, r.poss_dup);
      return true;
    }
    catch (const std::exception &e)
    {
      err = e.what();
      return false;
    }
  }

  bool parse_session_reject(const FIX::Message &m, SessionReject &r, std::string &err)
  {
    try
    {
      if (!need(m, 45, err))
        return false;
      r.ref_seq = uint64_t(std::strtoull(opt(m, 45).c_str(), nullptr, 10));
      r.ref_msg_type = opt(m, 372);
      r.ref_tag = opt_int(m, 371);
      r.reason = opt_int(m, 373);
      r.text = opt(m, 58);
      return true;
    }
    catch (const std::exception &e)
    {
      err = e.what();
      return false;
    }
  }

  std::string utc_now()
  {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
    std::time_t t = std::time_t(ms / 1000);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char b[32];
    std::snprintf(b, sizeof b, "%04d%02d%02d-%02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, int(ms % 1000));
    return b;
  }

  std::string utc_date()
  {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char b[16];
    std::snprintf(b, sizeof b, "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return b;
  }

  uint64_t utc_ns(const std::string &ts)
  {
    int Y, M, D, h, m, s;
    if (ts.size() < 17 || std::sscanf(ts.c_str(), "%4d%2d%2d-%2d:%2d:%2d", &Y, &M, &D, &h, &m, &s) != 6)
      return 0;
    std::tm tm{};
    tm.tm_year = Y - 1900;
    tm.tm_mon = M - 1;
    tm.tm_mday = D;
    tm.tm_hour = h;
    tm.tm_min = m;
    tm.tm_sec = s;
    uint64_t ns = uint64_t(timegm(&tm)) * 1'000'000'000ULL;
    if (ts.size() > 18 && ts[17] == '.')
    {
      uint64_t frac = 0;
      int digits = 0;
      for (size_t i = 18; i < ts.size() && digits < 9 && std::isdigit((unsigned char)ts[i]); ++i, ++digits)
        frac = frac * 10 + uint64_t(ts[i] - '0');
      for (; digits < 9; ++digits)
        frac *= 10;
      ns += frac;
    }
    return ns;
  }

  int decimals_of(double tick)
  {
    int d = 0;
    while (d < 9 && std::fabs(tick * std::pow(10.0, d) - std::round(tick * std::pow(10.0, d))) > 1e-9)
      ++d;
    return d;
  }

  std::string decimal(int64_t ticks, double tick)
  {
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", decimals_of(tick), double(ticks) * tick);
    return b;
  }
}
