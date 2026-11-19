/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "okx_oe/OkxConfig.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace okx::oe
{
  namespace
  {
    // Every WebSocket host the V5 documentation lists, demo first.
    const std::set<std::string> kDemoWs = {"wspap.okx.com"};
    const std::set<std::string> kProductionWs = {"ws.okx.com", "wsaws.okx.com", "wsus.okx.com", "wseea.okx.com"};
  }

  Headers Endpoint::headers() const
  {
    Headers h;
    if (demo)
      h["x-simulated-trading"] = "1";
    return h;
  }

  bool Endpoint::check(std::string &err) const
  {
    if (demo)
    {
      if (!kDemoWs.count(ws_host))
      {
        err = "environment = demo but ws_host " + ws_host + " is not OKX's demo host wspap.okx.com";
        return false;
      }
      if (rest_host.empty())
      {
        err = "no rest_host";
        return false;
      }
      return true;
    }
    if (!allow_production)
    {
      err = "environment = production is refused (allow_production is not set)";
      return false;
    }
    if (kDemoWs.count(ws_host))
    {
      err = "environment = production but ws_host is the demo host";
      return false;
    }
    if (!kProductionWs.count(ws_host))
    {
      err = "unknown ws_host " + ws_host;
      return false;
    }
    return true;
  }

  bool require_demo_header(const Endpoint &ep, const Headers &h, std::string &err)
  {
    if (!ep.demo)
      return true;
    auto it = h.find("x-simulated-trading");
    if (it == h.end() || it->second != "1")
    {
      err = "demo request without x-simulated-trading: 1 refused";
      return false;
    }
    return true;
  }

  int decimals(const std::string &dec)
  {
    auto p = dec.find('.');
    if (p == std::string::npos)
      return 0;
    size_t end = dec.size();
    while (end > p + 1 && dec[end - 1] == '0')
      --end;
    return int(end - p - 1);
  }

  std::string scaled(int64_t n, const std::string &unit)
  {
    const int d = decimals(unit);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", d, double(n) * std::stod(unit));
    return buf;
  }

  int64_t units_of(const std::string &value, const std::string &unit, bool &exact)
  {
    const long double v = std::stold(value), u = std::stold(unit);
    const long double q = v / u;
    const int64_t n = int64_t(std::llround(double(q)));
    exact = std::fabs(double(q - (long double)n)) < 1e-6;
    return n;
  }

  bool OkxOMConfig::from_file(const std::string &path, OkxOMConfig &out, std::string &err)
  {
    boost::property_tree::ptree pt;
    try
    {
      boost::property_tree::read_ini(path, pt);
    }
    catch (const std::exception &e)
    {
      err = e.what();
      return false;
    }
    try
    {
      const auto env = pt.get<std::string>("okx.environment", "demo");
      if (env != "demo" && env != "production")
      {
        err = "okx.environment must be demo or production";
        return false;
      }
      out.ep.demo = env == "demo";
      out.ep.ws_host = pt.get<std::string>("okx.ws_host", out.ep.demo ? "wspap.okx.com" : "ws.okx.com");
      out.ep.rest_host = pt.get<std::string>("okx.rest_host", "openapi.okx.com");
      out.ep.allow_production = pt.get<bool>("okx.allow_production", false);
      out.key_file = pt.get<std::string>("okx.key_file", "");
      out.reconnect_ms = pt.get<int>("okx.reconnect_ms", 2000);
      out.post_only = pt.get<bool>("okx.post_only", false);
      out.max_orders_per_sec = pt.get<int>("okx.max_orders_per_sec", 10);
      out.max_order_units = pt.get<int>("okx.max_order_units", 0);
      out.max_order_notional = pt.get<double>("okx.max_order_notional", 0);
      out.cancel_all_after_s = pt.get<int>("okx.cancel_all_after_s", 0);
      out.sim = pt.get<bool>("okx.sim", false);
      for (const auto &[name, s] : pt)
      {
        if (name.rfind("instrument.", 0) != 0)
          continue;
        Instrument i;
        i.asset = s.get<std::string>("asset");
        i.inst_id = s.get<std::string>("inst_id");
        i.inst_id_code = s.get<int64_t>("inst_id_code");
        i.inst_type = s.get<std::string>("inst_type");
        i.td_mode = s.get<std::string>("td_mode", i.inst_type == "SPOT" ? "cash" : "cross");
        i.tick = s.get<std::string>("tick");
        i.qty_unit = s.get<std::string>("qty_unit");
        i.venue = i.inst_type == "SPOT" ? en::x::OKX : en::x::OKXF;
        out.instruments.push_back(i);
      }
    }
    catch (const std::exception &e)
    {
      err = e.what();
      return false;
    }
    if (out.key_file.rfind("~/", 0) == 0)
      out.key_file = std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + out.key_file.substr(1);
    if (out.key_file.empty() && !out.sim)
      out.key_file = std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + "/.kaspar/keys/okx_demo.json";
    return true;
  }

  bool OkxOMConfig::valid(std::string &err) const
  {
    if (!sim && !ep.check(err))
      return false;
    if (instruments.empty())
    {
      err = "no instruments";
      return false;
    }
    for (const auto &i : instruments)
    {
      if (i.inst_id.empty() || i.inst_id_code <= 0)
      {
        err = "instrument " + i.asset + ": inst_id and inst_id_code are required";
        return false;
      }
      if (i.inst_type != "SPOT" && i.inst_type != "SWAP")
      {
        err = "instrument " + i.inst_id + ": inst_type must be SPOT or SWAP";
        return false;
      }
      try
      {
        if (!(i.tick_d() > 0) || !(i.unit_d() > 0))
        {
          err = "instrument " + i.inst_id + ": tick and qty_unit must be positive";
          return false;
        }
      }
      catch (...)
      {
        err = "instrument " + i.inst_id + ": tick and qty_unit must be decimal numbers";
        return false;
      }
    }
    if (cancel_all_after_s != 0 && (cancel_all_after_s < 10 || cancel_all_after_s > 120))
    {
      err = "cancel_all_after_s must be 0 or 10..120 (OKX Cancel All After)";
      return false;
    }
    if (reconnect_ms < 0)
    {
      err = "reconnect_ms must not be negative";
      return false;
    }
    return true;
  }

  const Instrument *OkxOMConfig::by_sym(int sym) const
  {
    for (const auto &i : instruments)
      if (i.sym == sym)
        return &i;
    return nullptr;
  }

  const Instrument *OkxOMConfig::by_inst_id(const std::string &id) const
  {
    for (const auto &i : instruments)
      if (i.inst_id == id)
        return &i;
    return nullptr;
  }
}
