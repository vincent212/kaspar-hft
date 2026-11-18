/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_post_trade/TradeMap.hpp"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "lmax_fix/LmaxFix.hpp"

namespace lmax::pt
{
  using post_trade::Event;
  using post_trade::Side;
  using post_trade::msg::TradeCapture;

  bool decimal_text(const std::string &s, int64_t &m, int &d)
  {
    m = 0;
    d = 0;
    if (s.empty())
      return false;
    size_t i = 0;
    bool neg = false, dot = false, digits = false;
    if (s[0] == '-')
    {
      neg = true;
      ++i;
    }
    for (; i < s.size(); ++i)
    {
      const char c = s[i];
      if (c == '.' && !dot)
        dot = true;
      else if (c >= '0' && c <= '9')
      {
        m = m * 10 + (c - '0');
        digits = true;
        if (dot)
          ++d;
      }
      else
        return false;
    }
    if (neg)
      m = -m;
    return digits;
  }

  std::string trade_id(const std::string &order_id, const std::string &secondary, const std::string &exec_id)
  {
    const bool sec = !secondary.empty() && secondary != "0";
    return order_id + ":" + (sec ? secondary : exec_id);
  }

  namespace
  {
    Side side_of(char c) { return c == '1' ? Side::Buy : c == '2' ? Side::Sell : Side::Unknown; }

    bool common(const std::string &security_id, const std::string &qty_text, double qty, const std::string &px_text,
                double px, const std::string &transact_time, const MapContext &ctx, TradeCapture &t,
                std::string &err)
    {
      t.venue = ctx.venue;
      t.symbol = security_id;
      double unit = 0;
      if (auto it = ctx.instruments.find(security_id); it != ctx.instruments.end())
      {
        t.asset = it->second.asset;
        unit = it->second.unit;
      }
      if (unit > 0)
      {
        int64_t units;
        if (!fix::to_steps(qty, unit, units))
          return err = "quantity " + qty_text + " is not a whole number of units of " + std::to_string(unit), false;
        t.qty = units;
      }
      else
        return err = "instrument " + security_id + " not configured", false;
      std::string p = px_text;
      if (p.empty())
      {
        char b[32];
        std::snprintf(b, sizeof b, "%.8f", px);
        p = b;
      }
      if (!decimal_text(p, t.price_mantissa, t.price_decimals))
        return err = "price '" + p + "' is not a decimal", false;
      const uint64_t ns = fix::utc_timestamp_ns(transact_time);
      if (!ns)
        return err = "bad TransactTime '" + transact_time + "'", false;
      const uint64_t day = 86400ULL * 1000000000ULL;
      t.trade_time_ns = int64_t(ns % day);
      return true;
    }

    uint32_t yyyymmdd(const std::string &s) { return s.size() >= 8 ? uint32_t(std::atoi(s.substr(0, 8).c_str())) : 0; }
  }

  bool from_tcr(const om::TradeCaptureReport &r, const MapContext &ctx, TradeCapture &t, std::string &err)
  {
    t = TradeCapture{};
    if (r.sides.empty())
      return err = "TradeCaptureReport without a side", false;
    const auto &s = r.sides[0];
    t.trade_id = trade_id(s.order_id, r.secondary_exec_id, r.exec_id);
    t.report_id = "N:" + t.trade_id;
    t.event = Event::New;
    t.venue_order_id = s.order_id;
    t.side = side_of(s.side);
    t.account = s.account;
    t.poss_dup = r.poss_dup;
    t.raw = r.raw;
    t.trade_date = yyyymmdd(r.trade_date);
    if (!common(r.security_id, r.last_qty_text, r.last_qty, r.last_px_text, r.last_px, r.transact_time, ctx, t, err))
      return false;
    return true;
  }

  bool from_exec(const om::ExecReport &r, const MapContext &ctx, TradeCapture &t, std::string &err)
  {
    t = TradeCapture{};
    const bool has_secondary = !r.secondary_exec_id.empty() && r.secondary_exec_id != "0";
    switch (r.exec_type)
    {
    case 'F':
      t.event = Event::New;
      t.trade_id = trade_id(r.order_id, r.secondary_exec_id, r.exec_id);
      t.report_id = "N:" + t.trade_id;
      break;
    case 'G':
    case 'H':
      if (!has_secondary)
        return err = std::string("Trade") + (r.exec_type == 'G' ? "Correct" : "Cancel") + " " + r.exec_id +
                     " has no SecondaryExecID: cannot tell which trade it changes",
               false;
      t.event = r.exec_type == 'G' ? Event::Amend : Event::Cancel;
      t.trade_id = trade_id(r.order_id, r.secondary_exec_id, r.exec_id);
      t.ref_trade_id = t.trade_id;
      t.report_id = std::string(1, r.exec_type) + ":" + r.exec_id;
      break;
    default:
      return err = std::string("ExecType ") + r.exec_type + " is not a trade event", false;
    }
    t.venue_order_id = r.order_id;
    t.side = side_of(r.side);
    t.account = r.account;
    t.poss_dup = r.poss_dup;
    t.raw = r.raw;
    t.settle_date = yyyymmdd(r.settl_date);
    t.trade_date = yyyymmdd(r.transact_time); // ExecutionReport has no TradeDate: the UTC date of TransactTime
    if (r.exec_type == 'H' && !r.has_last)
    {
      // A cancel may leave out the economics; the ledger fills them in.
      t.symbol = r.security_id;
      if (auto it = ctx.instruments.find(r.security_id); it != ctx.instruments.end())
        t.asset = it->second.asset;
      t.venue = ctx.venue;
      return true;
    }
    if (!r.has_last)
      return err = "trade report without LastQty/LastPx", false;
    return common(r.security_id, r.last_qty_text, r.last_qty, r.last_px_text, r.last_px, r.transact_time, ctx, t,
                  err);
  }

  namespace
  {
    int64_t days_from_civil(int y, unsigned m, unsigned d)
    {
      y -= m <= 2;
      const int64_t era = (y >= 0 ? y : y - 399) / 400;
      const unsigned yoe = unsigned(y - era * 400);
      const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
      const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
      return era * 146097 + int64_t(doe) - 719468;
    }
    // Day of week of a civil date, 0 = Sunday.
    int dow(int y, unsigned m, unsigned d)
    {
      const int64_t z = days_from_civil(y, m, d);
      return int((z + 4) % 7 + 7) % 7;
    }
    // US daylight saving, as UTC seconds: from the second Sunday of March at
    // 02:00 EST (07:00 UTC) to the first Sunday of November at 02:00 EDT
    // (06:00 UTC).
    bool ny_dst(int64_t utc_s)
    {
      const time_t t = time_t(utc_s);
      tm g;
      gmtime_r(&t, &g);
      const int y = g.tm_year + 1900;
      unsigned mar = 1 + (7 - dow(y, 3, 1)) % 7 + 7;
      unsigned nov = 1 + (7 - dow(y, 11, 1)) % 7;
      const int64_t start = days_from_civil(y, 3, mar) * 86400 + 7 * 3600;
      const int64_t end = days_from_civil(y, 11, nov) * 86400 + 6 * 3600;
      return utc_s >= start && utc_s < end;
    }
  }

  uint64_t trading_day_start_ns(uint64_t utc_ns)
  {
    const int64_t s = int64_t(utc_ns / 1000000000ULL);
    const int64_t off = ny_dst(s) ? -4 * 3600 : -5 * 3600;
    const int64_t local = s + off;
    int64_t day = local / 86400 * 86400;
    int64_t start_local = day + 17 * 3600;
    if (local < start_local)
      start_local -= 86400;
    return uint64_t(start_local - off) * 1000000000ULL;
  }

  std::string utc_timestamp(uint64_t ns)
  {
    time_t t = time_t(ns / 1000000000ULL);
    tm g;
    gmtime_r(&t, &g);
    char b[32];
    std::snprintf(b, sizeof b, "%04d%02d%02d-%02d:%02d:%02d.%03d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
                  g.tm_hour, g.tm_min, g.tm_sec, int(ns / 1000000 % 1000));
    return b;
  }
}
