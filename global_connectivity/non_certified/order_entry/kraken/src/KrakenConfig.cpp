/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_oe/KrakenConfig.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace kraken::oe
{
  bool Endpoint::check(std::string &err) const
  {
    if (rest_host.empty())
    {
      err = "rest_host is empty";
      return false;
    }
    if (testnet)
    {
      if (product == Product::Spot)
      {
        err = "Kraken has no spot testnet: product = spot needs testnet = false (production, owner only)";
        return false;
      }
      if (rest_host != kFuturesDemoHost)
      {
        err = "testnet = true but rest_host is " + rest_host + ", not " + kFuturesDemoHost + ": refused";
        return false;
      }
      return true;
    }
    if (product == Product::Spot && ws_host.empty())
    {
      err = "spot needs ws_host (ws-auth.kraken.com)";
      return false;
    }
    if (product == Product::Futures && rest_host == kFuturesDemoHost)
    {
      err = "testnet = false with the demo host: set testnet = true";
      return false;
    }
    return true;
  }

  bool load_settings(const std::string &path, Settings &out, std::string &err)
  {
    namespace pt = boost::property_tree;
    pt::ptree t;
    try
    {
      pt::read_ini(path, t);
    }
    catch (const std::exception &e)
    {
      err = "cannot read " + path + ": " + e.what();
      return false;
    }
    auto s = t.get_child_optional("kraken");
    if (!s)
    {
      err = path + ": no [kraken] section";
      return false;
    }
    try
    {
      Settings c;
      const std::string product = s->get<std::string>("product", "futures");
      if (product == "spot")
        c.ep.product = Product::Spot;
      else if (product == "futures")
        c.ep.product = Product::Futures;
      else
      {
        err = "product must be spot or futures";
        return false;
      }
      c.ep.testnet = s->get<bool>("testnet", true);
      c.ep.rest_host = s->get<std::string>("rest_host", c.ep.product == Product::Futures
                                                            ? (c.ep.testnet ? kFuturesDemoHost : kFuturesProdHost)
                                                            : kSpotRestHost);
      c.ep.ws_host = s->get<std::string>("ws_host", c.ep.product == Product::Spot ? kSpotWsAuthHost : "");
      c.key_file = s->get<std::string>("key_file", "");
      if (!c.key_file.empty() && c.key_file[0] == '~')
        if (const char *h = std::getenv("HOME"))
          c.key_file = h + c.key_file.substr(1);
      c.poll_ms = s->get<int>("poll_ms", 1000);
      c.cancel_after_s = s->get<int>("cancel_after_s", 0);
      c.sim = s->get<bool>("sim", false);
      c.om.qty_unit = s->get<double>("qty_unit", 1e-8);
      c.om.clid_prefix = s->get<std::string>("clid_prefix", "");
      c.om.day_as_gtc = s->get<bool>("day_as_gtc", true);
      c.om.post_only = s->get<bool>("post_only", false);
      c.om.reconnect_ms = s->get<int>("reconnect_ms", 2000);
      c.om.max_order_units = s->get<int64_t>("max_order_units", 0);
      c.om.max_order_notional = s->get<double>("max_order_notional", 0);
      c.om.max_orders_per_sec = s->get<int>("max_orders_per_sec", 0);
      // symbols = ASSET:VENUE_SYMBOL,ASSET:VENUE_SYMBOL
      std::vector<std::string> pairs;
      const std::string syms = s->get<std::string>("symbols", "");
      if (!syms.empty())
        boost::split(pairs, syms, boost::is_any_of(","));
      for (auto &p : pairs)
      {
        boost::trim(p);
        auto colon = p.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 == p.size())
        {
          err = "symbols entry '" + p + "' is not ASSET:VENUE_SYMBOL";
          return false;
        }
        c.om.symbols[p.substr(0, colon)] = p.substr(colon + 1);
      }
      if (!c.sim && !c.ep.check(err))
        return false;
      if (!c.sim && c.key_file.empty())
      {
        err = "key_file is required unless sim = true";
        return false;
      }
      if (c.poll_ms < 200)
      {
        err = "poll_ms below 200 (Kraken Futures rate limits)";
        return false;
      }
      if (!c.om.valid(err))
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
}
