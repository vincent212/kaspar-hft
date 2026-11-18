/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "deribit_oe/DeribitConfig.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <nlohmann/json.hpp>

namespace deribit::oe
{
  namespace
  {
    std::string home() { return std::getenv("HOME") ? std::getenv("HOME") : "."; }
    std::string expand(const std::string &p) { return p.rfind("~/", 0) == 0 ? home() + p.substr(1) : p; }
  }

  bool Endpoint::check(std::string &err) const
  {
    if (test)
    {
      if (host != kTestHost)
      {
        err = "environment = test but host " + host + " is not Deribit's test host " + kTestHost;
        return false;
      }
      return true;
    }
    if (!allow_production)
    {
      err = "environment = production is refused (allow_production is not set)";
      return false;
    }
    if (host != kProductionHost)
    {
      err = "environment = production but host " + host + " is not " + kProductionHost;
      return false;
    }
    return true;
  }

  std::string default_key_path() { return home() + "/.kaspar/keys/deribit_test.json"; }

  bool load_keys(const std::string &path, Keys &out, std::string &err)
  {
    std::ifstream f(path);
    if (!f)
    {
      err = "key file " + path + " cannot be read";
      return false;
    }
    try
    {
      auto j = nlohmann::json::parse(f);
      out.client_id = j.value("client_id", "");
      out.client_secret = j.value("client_secret", "");
    }
    catch (...)
    {
      err = "key file " + path + " is not valid JSON";
      return false;
    }
    if (!out.complete())
    {
      err = "key file " + path + " needs client_id and client_secret";
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
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals(unit), double(n) * std::stod(unit));
    return buf;
  }

  int64_t units_of(double value, const std::string &unit, bool &exact)
  {
    const long double q = (long double)value / std::stold(unit);
    const int64_t n = int64_t(std::llround(double(q)));
    exact = std::fabs(double(q - (long double)n)) < 1e-6;
    return n;
  }

  bool DeribitOMConfig::from_file(const std::string &path, DeribitOMConfig &out, std::string &err)
  {
    boost::property_tree::ptree pt;
    try
    {
      boost::property_tree::read_ini(path, pt);
      const auto env = pt.get<std::string>("deribit.environment", "test");
      if (env != "test" && env != "production")
      {
        err = "deribit.environment must be test or production";
        return false;
      }
      out.ep.test = env == "test";
      out.ep.host = pt.get<std::string>("deribit.host", out.ep.test ? kTestHost : kProductionHost);
      out.ep.allow_production = pt.get<bool>("deribit.allow_production", false);
      out.key_file = expand(pt.get<std::string>("deribit.key_file", ""));
      out.reconnect_ms = pt.get<int>("deribit.reconnect_ms", 2000);
      out.heartbeat_s = pt.get<int>("deribit.heartbeat_s", 30);
      out.cancel_on_disconnect = pt.get<bool>("deribit.cancel_on_disconnect", true);
      out.post_only = pt.get<bool>("deribit.post_only", false);
      out.max_orders_per_sec = pt.get<int>("deribit.max_orders_per_sec", 5);
      out.max_order_units = pt.get<int>("deribit.max_order_units", 0);
      out.max_order_notional = pt.get<double>("deribit.max_order_notional", 0);
      out.sim = pt.get<bool>("deribit.sim", false);
      for (const auto &[name, s] : pt)
      {
        if (name.rfind("instrument.", 0) != 0)
          continue;
        Instrument i;
        i.asset = s.get<std::string>("asset");
        i.name = s.get<std::string>("instrument");
        i.currency = s.get<std::string>("currency");
        i.tick = s.get<std::string>("tick");
        i.qty_unit = s.get<std::string>("qty_unit");
        i.inverse = s.get<bool>("inverse", true);
        out.instruments.push_back(i);
      }
    }
    catch (const std::exception &e)
    {
      err = e.what();
      return false;
    }
    if (out.key_file.empty() && !out.sim)
      out.key_file = default_key_path();
    return true;
  }

  bool DeribitOMConfig::valid(std::string &err) const
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
      if (i.name.empty() || i.currency.empty())
      {
        err = "instrument " + i.asset + ": instrument and currency are required";
        return false;
      }
      try
      {
        if (!(i.tick_d() > 0) || !(i.unit_d() > 0))
        {
          err = "instrument " + i.name + ": tick and qty_unit must be positive";
          return false;
        }
      }
      catch (...)
      {
        err = "instrument " + i.name + ": tick and qty_unit must be decimal numbers";
        return false;
      }
    }
    if (heartbeat_s < 10)
    {
      err = "heartbeat_s must be at least 10 (public/set_heartbeat)";
      return false;
    }
    if (reconnect_ms < 0)
    {
      err = "reconnect_ms must not be negative";
      return false;
    }
    return true;
  }

  const Instrument *DeribitOMConfig::by_sym(int sym) const
  {
    for (const auto &i : instruments)
      if (i.sym == sym)
        return &i;
    return nullptr;
  }

  const Instrument *DeribitOMConfig::by_name(const std::string &n) const
  {
    for (const auto &i : instruments)
      if (i.name == n)
        return &i;
    return nullptr;
  }
}
