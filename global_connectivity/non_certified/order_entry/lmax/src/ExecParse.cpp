/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_om/ExecParse.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "quickfix/Message.h"

#include "lmax_fix/LmaxFix.hpp"

namespace lmax::om
{
  namespace
  {
    using FIX::message_order;

    FIX::Message make(const char *type, const int *body)
    {
      FIX::Message m{message_order(message_order::header), message_order(message_order::trailer),
                     message_order(body)};
      m.getHeader().setField(8, "FIX.4.4");
      m.getHeader().setField(35, type);
      return m;
    }
    void opt(FIX::FieldMap &m, int tag, const std::string &v)
    {
      if (!v.empty())
        m.setField(tag, v);
    }
    std::string s(const FIX::FieldMap &m, int tag) { return m.isSetField(tag) ? m.getField(tag) : std::string(); }
    char c(const FIX::FieldMap &m, int tag)
    {
      auto v = s(m, tag);
      return v.empty() ? 0 : v[0];
    }
    double d(const FIX::FieldMap &m, int tag) { return m.isSetField(tag) ? std::strtod(m.getField(tag).c_str(), nullptr) : 0; }
    void header(const FIX::Message &m, uint64_t &seq, bool &poss_dup)
    {
      const auto &h = m.getHeader();
      seq = h.isSetField(34) ? std::strtoull(h.getField(34).c_str(), nullptr, 10) : 0;
      poss_dup = h.isSetField(43) && h.getField(43) == "Y";
    }
    bool need(const FIX::FieldMap &m, std::initializer_list<int> tags, std::string &err, const char *what)
    {
      for (int t : tags)
        if (!m.isSetField(t))
          return err = std::string(what) + " without required tag " + std::to_string(t), false;
      return true;
    }
  }

  int decimals_of(double step)
  {
    int n = 0;
    while (n < 10 && std::fabs(step * std::pow(10.0, n) - std::round(step * std::pow(10.0, n))) > 1e-9)
      ++n;
    return n;
  }

  std::string decimal(double v, double step)
  {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", decimals_of(step), v);
    return b;
  }

  std::string utc_now()
  {
    const uint64_t ns = fix::now_ns();
    time_t t = time_t(ns / 1000000000ULL);
    tm g;
    gmtime_r(&t, &g);
    char b[32];
    std::snprintf(b, sizeof b, "%04d%02d%02d-%02d:%02d:%02d.%03d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
                  g.tm_hour, g.tm_min, g.tm_sec, int(ns / 1000000 % 1000));
    return b;
  }

  // ---- builders (field order as in LMAX's fix-spec.json) -------------------------------

  FIX::Message build_new_order(const NewOrderReq &r)
  {
    static const int body[] = {11, 100, 48, 22, 55, 1, 54, 18, 60, 38, 40, 44, 99, 59, 1089, 0};
    FIX::Message m = make("D", body);
    m.setField(11, r.clordid);
    m.setField(48, r.security_id);
    m.setField(22, "8");
    opt(m, 1, r.account);
    m.setField(54, std::string(1, r.side));
    if (r.reinstate) // absent: LMAX's default, cancel on disconnect (spec, Cancel on Disconnect)
      m.setField(18, "H");
    m.setField(60, r.transact_time.empty() ? utc_now() : r.transact_time);
    m.setField(38, r.qty);
    m.setField(40, std::string(1, r.ord_type));
    opt(m, 44, r.px);
    opt(m, 99, r.stop_px);
    m.setField(59, std::string(1, r.tif));
    return m;
  }

  FIX::Message build_cancel(const CancelReq &r)
  {
    static const int body[] = {1, 41, 11, 48, 22, 55, 54, 60, 38, 0};
    FIX::Message m = make("F", body);
    opt(m, 1, r.account);
    m.setField(41, r.orig_clordid);
    m.setField(11, r.clordid);
    m.setField(48, r.security_id);
    m.setField(22, "8");
    m.setField(54, std::string(1, r.side));
    m.setField(60, r.transact_time.empty() ? utc_now() : r.transact_time);
    opt(m, 38, r.qty);
    return m;
  }

  FIX::Message build_replace(const ReplaceReq &r)
  {
    static const int body[] = {41, 1, 11, 48, 22, 55, 54, 60, 38, 18, 40, 44, 59, 0};
    FIX::Message m = make("G", body);
    m.setField(41, r.orig_clordid);
    opt(m, 1, r.account);
    m.setField(11, r.clordid);
    m.setField(48, r.security_id);
    m.setField(22, "8");
    m.setField(54, std::string(1, r.side));
    m.setField(60, r.transact_time.empty() ? utc_now() : r.transact_time);
    m.setField(38, r.qty);
    if (r.reinstate) // absent: LMAX's default, cancel on disconnect (spec, Cancel on Disconnect)
      m.setField(18, "H");
    m.setField(40, std::string(1, r.ord_type));
    m.setField(44, r.px);
    m.setField(59, std::string(1, r.tif));
    return m;
  }

  FIX::Message build_status(const StatusReq &r)
  {
    static const int body[] = {11, 1, 790, 48, 22, 55, 54, 0};
    FIX::Message m = make("H", body);
    m.setField(11, r.clordid);
    opt(m, 1, r.account);
    opt(m, 790, r.req_id);
    m.setField(48, r.security_id);
    m.setField(22, "8");
    m.setField(54, std::string(1, r.side));
    return m;
  }

  FIX::Message build_tcr_request(const TcrReq &r)
  {
    static const int body[] = {568, 569, 263, 1, 580, 0};
    FIX::Message m = make("AD", body);
    m.setField(568, r.req_id);
    m.setField(569, "1");
    m.setField(263, r.subscribe ? "1" : "0");
    opt(m, 1, r.account);
    static const int dates[] = {60, 0};
    for (const std::string *t : {&r.from, &r.to})
      if (!t->empty())
      {
        FIX::Group g(580, 60, dates);
        g.setField(60, *t);
        m.addGroup(g);
      }
    return m;
  }

  // ---- parsers -------------------------------------------------------------------------

  bool parse_exec(const FIX::Message &m, ExecReport &r, std::string &err)
  {
    r = ExecReport{};
    if (s(m.getHeader(), 35) != "8")
      return err = "not an ExecutionReport", false;
    header(m, r.seq, r.poss_dup);
    // Required by LMAX's spec: OrderID, ExecID, ExecType, OrdStatus, Side,
    // LeavesQty, CumQty, AvgPx.
    if (!need(m, {37, 17, 150, 39, 54, 151, 14, 6}, err, "ExecutionReport"))
      return false;
    r.order_id = s(m, 37);
    r.secondary_exec_id = s(m, 527);
    r.clordid = s(m, 11);
    r.orig_clordid = s(m, 41);
    r.status_req_id = s(m, 790);
    r.exec_id = s(m, 17);
    r.exec_type = c(m, 150);
    r.ord_status = c(m, 39);
    if (m.isSetField(103))
      r.rej_reason = std::atoi(m.getField(103).c_str());
    r.account = s(m, 1);
    r.security_id = s(m, 48);
    r.symbol = s(m, 55);
    r.side = c(m, 54);
    r.ord_type = c(m, 40);
    r.tif = c(m, 59);
    r.qty = d(m, 38);
    r.px = d(m, 44);
    r.stop_px = d(m, 99);
    r.has_last = m.isSetField(32) && m.isSetField(31);
    r.last_qty = d(m, 32);
    r.last_px = d(m, 31);
    r.last_qty_text = s(m, 32);
    r.last_px_text = s(m, 31);
    r.leaves = d(m, 151);
    r.cum = d(m, 14);
    r.avg_px = d(m, 6);
    r.transact_time = s(m, 60);
    r.settl_date = s(m, 64);
    r.text = s(m, 58);
    r.commission = s(m, 12);
    r.comm_type = s(m, 13);
    r.comm_ccy = s(m, 479);
    if (r.exec_type == 'F' && !r.has_last)
      return err = "ExecutionReport Trade without LastQty/LastPx", false;
    return true;
  }

  bool parse_cancel_reject(const FIX::Message &m, CancelReject &r, std::string &err)
  {
    r = CancelReject{};
    if (s(m.getHeader(), 35) != "9")
      return err = "not an OrderCancelReject", false;
    header(m, r.seq, r.poss_dup);
    if (!need(m, {37, 11, 41, 39, 434}, err, "OrderCancelReject"))
      return false;
    r.order_id = s(m, 37);
    r.clordid = s(m, 11);
    r.orig_clordid = s(m, 41);
    r.ord_status = c(m, 39);
    r.response_to = c(m, 434);
    if (m.isSetField(102))
      r.reason = std::atoi(m.getField(102).c_str());
    r.account = s(m, 1);
    r.text = s(m, 58);
    return true;
  }

  bool parse_tcr(const FIX::Message &m, TradeCaptureReport &r, std::string &err)
  {
    r = TradeCaptureReport{};
    if (s(m.getHeader(), 35) != "AE")
      return err = "not a TradeCaptureReport", false;
    header(m, r.seq, r.poss_dup);
    if (!need(m, {32, 31, 75, 60, 552}, err, "TradeCaptureReport"))
      return false;
    r.req_id = s(m, 568);
    r.last_rpt = s(m, 912) == "Y";
    r.exec_id = s(m, 17);
    r.secondary_exec_id = s(m, 527);
    r.security_id = s(m, 48);
    r.symbol = s(m, 55);
    r.last_qty_text = s(m, 32);
    r.last_px_text = s(m, 31);
    r.last_qty = d(m, 32);
    r.last_px = d(m, 31);
    r.trade_date = s(m, 75);
    r.transact_time = s(m, 60);
    r.commission = s(m, 12);
    r.comm_type = s(m, 13);
    r.comm_ccy = s(m, 479);
    const int n = std::atoi(m.getField(552).c_str());
    for (int i = 1; i <= n; ++i)
    {
      FIX::Group g(552, 54);
      try
      {
        m.getGroup(i, g);
      }
      catch (const std::exception &)
      {
        return err = "NoSides says " + std::to_string(n) + ", side " + std::to_string(i) + " missing", false;
      }
      TradeCaptureReport::Side sd;
      sd.side = c(g, 54);
      sd.order_id = s(g, 37);
      sd.clordid = s(g, 11);
      sd.account = s(g, 1);
      if (!sd.side || sd.order_id.empty())
        return err = "TradeCaptureReport side without Side or OrderID", false;
      r.sides.push_back(sd);
    }
    return true;
  }

  bool parse_tcr_ack(const FIX::Message &m, TcrAck &r, std::string &err)
  {
    r = TcrAck{};
    if (s(m.getHeader(), 35) != "AQ")
      return err = "not a TradeCaptureReportRequestAck", false;
    if (!need(m, {568, 749, 750}, err, "TradeCaptureReportRequestAck"))
      return false;
    r.req_id = s(m, 568);
    r.result = std::atoi(s(m, 749).c_str());
    r.status = std::atoi(s(m, 750).c_str());
    if (m.isSetField(748))
      r.total = std::atoi(m.getField(748).c_str());
    r.subscription = c(m, 263);
    r.text = s(m, 58);
    return true;
  }
}
