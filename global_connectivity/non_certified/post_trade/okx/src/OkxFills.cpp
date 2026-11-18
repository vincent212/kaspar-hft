/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "okx_pt/OkxFills.hpp"

#include <cstdlib>
#include <ctime>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace okx::pt
{
  using json = nlohmann::json;

  namespace
  {
    std::string str(const json &j, const char *k)
    {
      auto it = j.find(k);
      if (it == j.end() || it->is_null())
        return {};
      return it->is_string() ? it->get<std::string>() : it->dump();
    }
  }

  bool parse_decimal(const std::string &s, int64_t &mantissa, int &decimals)
  {
    if (s.empty())
      return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '-')
    {
      neg = true;
      i = 1;
    }
    int64_t m = 0;
    int d = -1, digits = 0;
    for (; i < s.size(); ++i)
    {
      const char c = s[i];
      if (c == '.')
      {
        if (d >= 0)
          return false;
        d = 0;
        continue;
      }
      if (c < '0' || c > '9' || ++digits > 18)
        return false;
      m = m * 10 + (c - '0');
      if (d >= 0)
        ++d;
    }
    if (digits == 0)
      return false;
    mantissa = neg ? -m : m;
    decimals = d < 0 ? 0 : d;
    return true;
  }

  bool to_trade_capture(const json &f, const FillsConfig &cfg, bool from_rest, post_trade::msg::TradeCapture &out,
                        std::string &err)
  {
    const std::string inst = str(f, "instId"), trade = str(f, "tradeId"), sz = str(f, "fillSz");
    if (trade.empty() || sz.empty() || sz == "0")
    {
      err = "not a fill";
      return false;
    }
    const PtInstrument *ins = cfg.find(inst);
    if (!ins)
    {
      err = "instrument " + inst + " not configured";
      return false;
    }
    int64_t qm = 0, lm = 0;
    int qd = 0, ld = 0;
    if (!parse_decimal(sz, qm, qd) || !parse_decimal(ins->lot, lm, ld) || lm <= 0 || qm <= 0)
    {
      err = "fillSz '" + sz + "' or lot '" + ins->lot + "' not understood";
      return false;
    }
    // qty = sz / lot exactly: bring both to the larger number of decimals.
    const int dd = std::max(qd, ld);
    int64_t a = qm, b = lm;
    for (int k = qd; k < dd; ++k)
      a *= 10;
    for (int k = ld; k < dd; ++k)
      b *= 10;
    if (a % b != 0)
    {
      err = "fillSz " + sz + " is not a whole number of lots of " + ins->lot;
      return false;
    }
    int64_t pm = 0;
    int pd = 0;
    if (!parse_decimal(str(f, "fillPx"), pm, pd))
    {
      err = "fillPx '" + str(f, "fillPx") + "' not understood";
      return false;
    }
    const std::string side = str(f, "side");
    out = post_trade::msg::TradeCapture();
    out.venue = ins->venue;
    out.trade_id = inst + ":" + trade;
    out.report_id = out.trade_id; // the same fill from WebSocket and REST is one report
    out.report_seq = 0;
    out.event = post_trade::Event::New;
    out.venue_order_id = str(f, "ordId");
    out.symbol = inst;
    out.asset = ins->sym;
    out.side = side == "buy" ? post_trade::Side::Buy : side == "sell" ? post_trade::Side::Sell : post_trade::Side::Unknown;
    out.qty = a / b;
    out.price_mantissa = pm;
    out.price_decimals = pd;
    // OKX trades around the clock in UTC: the trade date is the UTC date.
    std::string t = str(f, "fillTime");
    if (t.empty())
      t = str(f, "ts");
    const int64_t ms = t.empty() ? 0 : std::stoll(t);
    const std::time_t s = std::time_t(ms / 1000);
    std::tm tm{};
    gmtime_r(&s, &tm);
    out.trade_date = uint32_t((tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
    out.trade_time_ns = (int64_t(tm.tm_hour) * 3600 + tm.tm_min * 60 + tm.tm_sec) * 1000000000ll + (ms % 1000) * 1000000ll;
    // account stays empty: an OKX fill names no account; clOrdId is in raw.
    out.poss_dup = from_rest;
    out.raw = f.dump();
    return true;
  }

  bool FillsConfig::from_file(const std::string &path, FillsConfig &out, std::string &err)
  {
    boost::property_tree::ptree pt;
    try
    {
      boost::property_tree::read_ini(path, pt);
      const auto env = pt.get<std::string>("okx.environment", "demo");
      out.ep.demo = env == "demo";
      out.ep.ws_host = pt.get<std::string>("okx.ws_host", out.ep.demo ? "wspap.okx.com" : "ws.okx.com");
      out.ep.rest_host = pt.get<std::string>("okx.rest_host", "openapi.okx.com");
      out.ep.allow_production = pt.get<bool>("okx.allow_production", false);
      out.key_file = pt.get<std::string>("okx.key_file", "~/.kaspar/keys/okx_demo.json");
      if (out.key_file.rfind("~/", 0) == 0)
        out.key_file = std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + out.key_file.substr(1);
      out.channel = pt.get<std::string>("post_trade.channel", "orders");
      out.reconcile_every_s = pt.get<int>("post_trade.reconcile_every_s", 60);
      out.reconcile_lookback_s = pt.get<int>("post_trade.reconcile_lookback_s", 3600);
      out.reconnect_ms = pt.get<int>("okx.reconnect_ms", 2000);
      out.sim = pt.get<bool>("okx.sim", false);
      for (const auto &[name, s] : pt)
      {
        if (name.rfind("instrument.", 0) != 0)
          continue;
        PtInstrument i;
        i.inst_id = s.get<std::string>("inst_id");
        i.asset = s.get<std::string>("asset", "");
        const auto type = s.get<std::string>("inst_type");
        i.lot = s.get<std::string>("lot_sz", type == "SPOT" ? "0.00000001" : "0.01");
        i.venue = type == "SPOT" ? en::x::OKX : en::x::OKXF;
        out.instruments.push_back(i);
      }
    }
    catch (const std::exception &e)
    {
      err = e.what();
      return false;
    }
    return true;
  }

  bool FillsConfig::valid(std::string &err) const
  {
    if (!sim && !ep.check(err))
      return false;
    if (channel != "orders" && channel != "fills")
    {
      err = "post_trade.channel must be orders or fills";
      return false;
    }
    if (instruments.empty())
    {
      err = "no instruments";
      return false;
    }
    if (reconcile_lookback_s <= 0 || reconcile_lookback_s > 3 * 86400)
    {
      err = "reconcile_lookback_s must be 1 s to 3 days (GET /api/v5/trade/fills covers 3 days)";
      return false;
    }
    return true;
  }

  const PtInstrument *FillsConfig::find(const std::string &inst_id) const
  {
    for (const auto &i : instruments)
      if (i.inst_id == inst_id)
        return &i;
    return nullptr;
  }
}
