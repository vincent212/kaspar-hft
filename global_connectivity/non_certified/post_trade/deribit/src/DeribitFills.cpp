/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "deribit_pt/DeribitFills.hpp"

#include <cstdlib>
#include <ctime>
#include <set>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace deribit::pt
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

  bool to_trade_capture(const json &t, const FillsConfig &cfg, bool from_rest, post_trade::msg::TradeCapture &out,
                        std::string &err)
  {
    const std::string inst = str(t, "instrument_name"), trade = str(t, "trade_id");
    if (trade.empty())
    {
      err = "no trade_id";
      return false;
    }
    const PtInstrument *ins = cfg.find(inst);
    if (!ins)
    {
      err = "instrument " + inst + " not configured";
      return false;
    }
    // amount and price are JSON numbers; nlohmann prints a double in its
    // shortest round-trip form ("63000.5", "0.15", "100"), which is the
    // decimal Deribit sent.
    int64_t qm = 0, um = 0;
    int qd = 0, ud = 0;
    std::string amount = str(t, "amount");
    if (amount.size() > 2 && amount.compare(amount.size() - 2, 2, ".0") == 0)
      amount.resize(amount.size() - 2);
    if (!parse_decimal(amount, qm, qd) || !parse_decimal(ins->qty_unit, um, ud) || um <= 0 || qm <= 0)
    {
      err = "amount '" + amount + "' or qty_unit '" + ins->qty_unit + "' not understood";
      return false;
    }
    const int dd = std::max(qd, ud);
    int64_t a = qm, b = um;
    for (int k = qd; k < dd; ++k)
      a *= 10;
    for (int k = ud; k < dd; ++k)
      b *= 10;
    if (a % b != 0)
    {
      err = "amount " + amount + " is not a whole number of units of " + ins->qty_unit;
      return false;
    }
    int64_t pm = 0;
    int pd = 0;
    std::string px = str(t, "price");
    if (px.size() > 2 && px.compare(px.size() - 2, 2, ".0") == 0)
      px.resize(px.size() - 2);
    if (!parse_decimal(px, pm, pd))
    {
      err = "price '" + px + "' not understood";
      return false;
    }
    const std::string dir = str(t, "direction");
    out = post_trade::msg::TradeCapture();
    out.venue = ins->venue;
    // trade_id is unique per currency (feed.md); Deribit's own ids usually
    // carry the currency as a prefix, but that is not documented.
    out.trade_id = ins->currency + ":" + trade;
    out.report_id = out.trade_id;   // the same trade from WebSocket and REST is one report
    out.report_seq = 0;
    out.event = post_trade::Event::New;
    out.venue_order_id = str(t, "order_id");
    out.symbol = inst;
    out.asset = ins->sym;
    out.side = dir == "buy" ? post_trade::Side::Buy : dir == "sell" ? post_trade::Side::Sell : post_trade::Side::Unknown;
    out.qty = a / b;
    out.price_mantissa = pm;
    out.price_decimals = pd;
    // Deribit trades around the clock; the trade date is the UTC date.
    const double msd = t.contains("timestamp") && t["timestamp"].is_number() ? t["timestamp"].get<double>() : 0;
    const int64_t ms = int64_t(msd);
    const std::time_t s = std::time_t(ms / 1000);
    std::tm tm{};
    gmtime_r(&s, &tm);
    out.trade_date = uint32_t((tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
    out.trade_time_ns =
        (int64_t(tm.tm_hour) * 3600 + tm.tm_min * 60 + tm.tm_sec) * 1000000000ll + (ms % 1000) * 1000000ll;
    out.account = str(t, "label");
    out.poss_dup = from_rest;
    out.raw = t.dump();
    return true;
  }

  bool FillsConfig::from_file(const std::string &path, FillsConfig &out, std::string &err)
  {
    oe::DeribitOMConfig om;
    if (!oe::DeribitOMConfig::from_file(path, om, err))
      return false;
    out.ep = om.ep;
    out.key_file = om.key_file;
    out.heartbeat_s = om.heartbeat_s;
    out.reconnect_ms = om.reconnect_ms;
    out.sim = om.sim;
    for (const auto &i : om.instruments)
      out.instruments.push_back({i.name, i.currency, i.asset, i.qty_unit, i.venue, -1});
    try
    {
      boost::property_tree::ptree pt;
      boost::property_tree::read_ini(path, pt);
      out.reconcile_every_s = pt.get<int>("post_trade.reconcile_every_s", 60);
      out.reconcile_lookback_s = pt.get<int>("post_trade.reconcile_lookback_s", 3600);
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
    if (instruments.empty())
    {
      err = "no instruments";
      return false;
    }
    if (reconcile_lookback_s <= 0)
    {
      err = "reconcile_lookback_s must be positive";
      return false;
    }
    if (heartbeat_s < 10)
    {
      err = "heartbeat_s must be at least 10";
      return false;
    }
    return true;
  }

  const PtInstrument *FillsConfig::find(const std::string &name) const
  {
    for (const auto &i : instruments)
      if (i.name == name)
        return &i;
    return nullptr;
  }

  std::vector<std::string> FillsConfig::currencies() const
  {
    std::set<std::string> s;
    for (const auto &i : instruments)
      s.insert(i.currency);
    return {s.begin(), s.end()};
  }
}
