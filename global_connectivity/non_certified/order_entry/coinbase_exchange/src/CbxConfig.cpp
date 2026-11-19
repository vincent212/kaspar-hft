/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdlib>
#include <iostream>
#include <sstream>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "coinbase_exchange_oe/act/CbxOM.hpp"

namespace coinbase_exchange::oe
{

// config/cbx_om_example.ini documents every key.
bool CbxOMConfig::from_file(const std::string &path, CbxOMConfig &out, std::string &err)
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
    auto env = parse_environment(pt.get<std::string>("session.environment", "sandbox"));
    if (!env)
    {
      err = "session.environment must be sandbox or production";
      return false;
    }
    auto &s = out.session;
    s.environment = *env;
    s.host = pt.get<std::string>("session.host", "");
    s.port = pt.get<int>("session.port", 0);
    s.key_file = expand_home(pt.get<std::string>("session.key_file", default_key_path()));
    auto cod = pt.get<std::string>("session.cancel_on_disconnect", "Y");
    s.cancel_on_disconnect = cod == "none" ? 0 : cod.empty() ? 0 : cod[0];
    s.heartbeat_s = pt.get<int>("session.heartbeat_s", 10);
    s.store_dir = expand_home(pt.get<std::string>("session.store_dir", ""));
    s.ca_file = expand_home(pt.get<std::string>("session.ca_file", ""));
    s.transport_dict = expand_home(pt.get<std::string>("session.transport_dict", ""));
    s.app_dict = expand_home(pt.get<std::string>("session.app_dict", ""));
    s.reconnect_s = pt.get<int>("session.reconnect_s", 5);

    auto day = pt.get<std::string>("orders.day_tif", "gtc");
    out.day_tif = day == "gtc" ? '1' : day == "gtd" ? '6' : day == "reject" ? 'R' : 0;
    out.post_only = pt.get<bool>("orders.post_only", false);
    auto stp = pt.get<std::string>("orders.self_trade", "D");
    out.self_trade = stp.empty() ? 'D' : stp[0];
    out.max_order_qty = pt.get<int>("orders.max_order_qty", 0);
    out.max_order_notional = pt.get<double>("orders.max_order_notional", 0);
    out.max_orders_per_sec = pt.get<int>("orders.max_orders_per_sec", 50);
    out.reconnect_ms = pt.get<int>("orders.reconnect_ms", 2000);
    out.logon_timeout_ms = pt.get<int>("orders.logon_timeout_ms", 15000);
    out.sim = pt.get<bool>("orders.sim", false);
    out.sim_config.fill = pt.get<bool>("sim.fill", true);
    out.sim_config.fill_delay_ms = pt.get<int>("sim.fill_delay_ms", 50);
    out.sim_config.fill_parts = pt.get<int>("sim.fill_parts", 1);

    std::stringstream list(pt.get<std::string>("orders.products", ""));
    std::string prod;
    while (std::getline(list, prod, ','))
    {
      if (prod.empty())
        continue;
      OEProduct p;
      if (!OEProduct::make(prod, pt.get<std::string>(prod + ".quote_increment"),
                           pt.get<std::string>(prod + ".base_increment"), pt.get<std::string>(prod + ".qty_unit", ""),
                           p, err))
        return false;
      out.products[prod] = p;
    }
  }
  catch (const std::exception &e)
  {
    err = e.what();
    return false;
  }
  return out.valid(err);
}
}

actors::Actor *create_CbxOM(const std::string &config_path, actors::Actor *som)
{
  coinbase_exchange::oe::CbxOMConfig cfg;
  std::string err;
  if (!coinbase_exchange::oe::CbxOMConfig::from_file(config_path, cfg, err))
  {
    std::cerr << "CbxOM: not created, cannot read " << config_path << ": " << err
              << ". Coinbase Exchange is unrouted." << std::endl;
    return nullptr;
  }
  return create_CbxOM(cfg, som);
}
