/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_pt/KrakenFills.hpp"

#include <cmath>
#include <ctime>

#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "frame/ref/RefData.hpp"
#include "kraken_md/KrakenJson.hpp"

namespace kraken::pt
{
  using json = nlohmann::json;
  namespace ptm = post_trade::msg;

  namespace
  {
    std::string text(const json &j, const char *k)
    {
      auto it = j.find(k);
      if (it == j.end() || it->is_null())
        return {};
      if (it->is_string())
        return it->get<std::string>();
      return kraken::num_text(*it);
    }

    void utc_date_time(uint64_t ns, uint32_t &date, int64_t &tod)
    {
      const time_t s = time_t(ns / 1'000'000'000ull);
      std::tm tm{};
      gmtime_r(&s, &tm);
      date = uint32_t((tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
      tod = int64_t(ns % 86'400'000'000'000ull);
    }

    // REST TradesHistory `time`: seconds with a fraction ("1688667796.8802").
    uint64_t seconds_text_to_ns(const std::string &t)
    {
      int64_t m = 0;
      int d = 0;
      if (!parse_decimal(t, m, d) || d > 9)
        return 0;
      int64_t ns = m;
      for (int i = d; i < 9; ++i)
        ns *= 10;
      return uint64_t(ns);
    }
  }

  bool FillsConfig::valid(std::string &err) const
  {
    if (!sim && !ep.check(err))
      return false;
    if (!sim && key_file.empty())
    {
      err = "key_file is required unless sim = true";
      return false;
    }
    if (instruments.empty())
    {
      err = "no instruments";
      return false;
    }
    for (const auto &i : instruments)
    {
      int64_t m;
      int d;
      if (!parse_decimal(i.lot, m, d) || m <= 0)
      {
        err = "instrument " + i.symbol + ": lot '" + i.lot + "' is not a positive decimal";
        return false;
      }
    }
    if (poll_ms < 200)
    {
      err = "poll_ms below 200";
      return false;
    }
    return true;
  }

  const PtInstrument *FillsConfig::find(const std::string &s) const
  {
    for (const auto &i : instruments)
      if (i.symbol == s || (!i.rest_pair.empty() && i.rest_pair == s))
        return &i;
    return nullptr;
  }

  bool FillsConfig::from_file(const std::string &path, FillsConfig &out, std::string &err)
  {
    namespace pt = boost::property_tree;
    pt::ptree t;
    try
    {
      pt::read_ini(path, t);
      auto s = t.get_child("kraken_fills");
      FillsConfig c;
      const std::string product = s.get<std::string>("product", "futures");
      c.ep.product = product == "spot" ? oe::Product::Spot : oe::Product::Futures;
      c.ep.testnet = s.get<bool>("testnet", true);
      c.ep.rest_host = s.get<std::string>(
          "rest_host", c.ep.product == oe::Product::Futures
                           ? (c.ep.testnet ? oe::kFuturesDemoHost : oe::kFuturesProdHost)
                           : oe::kSpotRestHost);
      c.ep.ws_host = s.get<std::string>("ws_host", c.ep.product == oe::Product::Spot ? oe::kSpotWsAuthHost : "");
      c.key_file = s.get<std::string>("key_file", "");
      if (!c.key_file.empty() && c.key_file[0] == '~')
        if (const char *h = std::getenv("HOME"))
          c.key_file = h + c.key_file.substr(1);
      c.poll_ms = s.get<int>("poll_ms", 1000);
      c.reconcile_every_s = s.get<int>("reconcile_every_s", 60);
      c.reconcile_lookback_s = s.get<int>("reconcile_lookback_s", 3600);
      c.reconnect_ms = s.get<int>("reconnect_ms", 2000);
      c.sim = s.get<bool>("sim", false);
      // instruments = SYMBOL|REST_PAIR|ASSET|LOT, ...
      std::vector<std::string> items;
      boost::split(items, s.get<std::string>("instruments", ""), boost::is_any_of(","));
      for (auto &it : items)
      {
        boost::trim(it);
        if (it.empty())
          continue;
        std::vector<std::string> f;
        boost::split(f, it, boost::is_any_of("|"));
        if (f.size() != 4)
        {
          err = "instruments entry '" + it + "' is not SYMBOL|REST_PAIR|ASSET|LOT";
          return false;
        }
        PtInstrument i;
        i.symbol = f[0];
        i.rest_pair = f[1];
        i.asset = f[2];
        i.lot = f[3];
        i.venue = c.ep.product == oe::Product::Spot ? en::x::KRAKEN : en::x::KRAKENF;
        if (auto a = frame::ref::RefData::get_asset(i.asset))
          i.sym = int(a->id);
        c.instruments.push_back(i);
      }
      if (!c.valid(err))
        return false;
      out = std::move(c);
      return true;
    }
    catch (const std::exception &e)
    {
      err = path + ": " + e.what();
      return false;
    }
  }

  bool to_trade_capture(const json &f, const std::string &key, Source src, const FillsConfig &cfg,
                        ptm::TradeCapture &out, std::string &err)
  {
    ptm::TradeCapture t;
    std::string symbol, qty, px, side, order_id;
    uint64_t ns = 0;
    switch (src)
    {
    case Source::SpotWs:
      if (text(f, "exec_type") != "trade")
      {
        err = "not a trade";
        return false;
      }
      t.trade_id = text(f, "exec_id");
      symbol = text(f, "symbol");
      qty = text(f, "last_qty");
      px = text(f, "last_price");
      side = text(f, "side");
      order_id = text(f, "order_id");
      ns = kraken::parse_rfc3339(text(f, "timestamp"));
      break;
    case Source::SpotRest:
      t.trade_id = key;
      symbol = text(f, "pair");
      qty = text(f, "vol");
      px = text(f, "price");
      side = text(f, "type");
      order_id = text(f, "ordertxid");
      ns = seconds_text_to_ns(text(f, "time"));
      break;
    case Source::FuturesPoll:
    case Source::FuturesRest:
      t.trade_id = text(f, "fill_id");
      symbol = text(f, "symbol");
      qty = text(f, "size");
      px = text(f, "price");
      side = text(f, "side");
      order_id = text(f, "order_id");
      ns = kraken::parse_rfc3339(text(f, "fillTime"));
      if (auto s = text(f, "sequence_id"); !s.empty())
        t.report_seq = std::strtoull(s.c_str(), nullptr, 10);
      break;
    }
    if (t.trade_id.empty())
    {
      err = "no fill identifier";
      return false;
    }
    const PtInstrument *ins = cfg.find(symbol);
    if (!ins)
    {
      err = "instrument " + symbol + " not configured";
      return false;
    }
    int64_t qm, lm;
    int qd, ld;
    if (!parse_decimal(qty, qm, qd) || !parse_decimal(ins->lot, lm, ld) || qm <= 0)
    {
      err = "unreadable quantity '" + qty + "'";
      return false;
    }
    // qty / lot, exactly: align the decimals.
    __int128 q = qm, l = lm;
    for (int i = qd; i < ld; ++i)
      q *= 10;
    for (int i = ld; i < qd; ++i)
      l *= 10;
    if (q % l)
    {
      err = "quantity " + qty + " is not a whole number of lots of " + ins->lot;
      return false;
    }
    if (!parse_decimal(px, t.price_mantissa, t.price_decimals))
    {
      err = "unreadable price '" + px + "'";
      return false;
    }
    t.venue = ins->venue;
    t.report_id = t.trade_id; // one report per fill; Kraken does not amend fills
    t.event = post_trade::Event::New;
    t.venue_order_id = order_id;
    t.symbol = ins->symbol;
    t.asset = ins->sym;
    t.side = side == "buy" ? post_trade::Side::Buy : side == "sell" ? post_trade::Side::Sell : post_trade::Side::Unknown;
    t.qty = int64_t(q / l);
    utc_date_time(ns, t.trade_date, t.trade_time_ns);
    t.poss_dup = from_rest(src);
    t.raw = key.empty() ? f.dump() : json{{key, f}}.dump();
    out = std::move(t);
    return true;
  }
}
