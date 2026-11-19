/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitOMConfig: reading and checking the order manager's settings
// (config/bybit_om_example.ini). Keys are read from key_file, outside the
// repository, unless sim.

#include <sstream>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "bybit_oe/BybitPrivate.hpp"
#include "bybit_oe/act/BybitOM.hpp"

namespace bybit::oe
{

bool BybitOMConfig::from_file(const std::string &path, BybitOMConfig &c, std::string &err)
{
  try
  {
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(path, pt);
    c.sim = pt.get<bool>("om.sim", false);
    c.session.testnet = pt.get<bool>("om.testnet", true);
    c.session.rest_host = pt.get<std::string>("om.rest_host", c.session.rest_host);
    c.session.ws_host = pt.get<std::string>("om.ws_host", c.session.ws_host);
    c.session.recv_window_ms = pt.get<int>("om.recv_window_ms", c.session.recv_window_ms);
    c.key_file = pt.get<std::string>("om.key_file", c.key_file);
    c.reconnect_ms = pt.get<int>("om.reconnect_ms", c.reconnect_ms);
    c.day_as_gtc = pt.get<bool>("om.day_as_gtc", c.day_as_gtc);
    c.max_order_qty = pt.get<int>("om.max_order_qty", 0);
    c.max_order_notional = pt.get<double>("om.max_order_notional", 0);
    c.max_orders_per_sec = pt.get<int>("om.max_orders_per_sec", c.max_orders_per_sec);
    c.sim_config.fill_delay_ms = pt.get<int>("om.sim_fill_delay_ms", c.sim_config.fill_delay_ms);
    c.sim_config.fill_parts = pt.get<int>("om.sim_fill_parts", c.sim_config.fill_parts);
    c.sim_config.fill = pt.get<bool>("om.sim_fill", c.sim_config.fill);
    if (auto ins = pt.get_child_optional("instruments"))
      for (const auto &[k, v] : *ins)
      {
        Instrument i;
        std::istringstream ss(v.get_value<std::string>());
        ss >> i.category >> i.symbol >> i.tick >> i.qty_unit;
        if (!ss)
        {
          err = "instrument " + k + ": want '<category> <symbol> <tick> <qty_unit>'";
          return false;
        }
        c.instruments[std::stoi(k)] = i;
      }
    if (!c.sim && !auth::load(c.key_file, c.session.credentials, err))
      return false;
  }
  catch (const std::exception &e)
  {
    err = e.what();
    return false;
  }
  return c.valid(err);
}

bool BybitOMConfig::valid(std::string &err) const
{
  if (instruments.empty())
  {
    err = "no instruments";
    return false;
  }
  for (const auto &[sym, i] : instruments)
  {
    if (i.category != "spot" && i.category != "linear")
    {
      err = "instrument " + std::to_string(sym) + ": category must be spot or linear";
      return false;
    }
    if (i.symbol.empty() || !(i.tick > 0) || !(i.qty_unit > 0))
    {
      err = "instrument " + std::to_string(sym) + ": symbol, tick and qty_unit are required";
      return false;
    }
  }
  if (sim)
    return true;
  if (session.rest_host.empty() || session.ws_host.empty())
  {
    err = "rest_host and ws_host are required";
    return false;
  }
  try
  {
    priv::guard(session.testnet, session.rest_host, session.ws_host);
  }
  catch (const std::exception &e)
  {
    err = e.what();
    return false;
  }
  if (session.credentials.empty())
  {
    err = "no API key loaded";
    return false;
  }
  if (session.recv_window_ms <= 0 || reconnect_ms <= 0)
  {
    err = "recv_window_ms and reconnect_ms must be positive";
    return false;
  }
  return true;
}

}
