/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <iostream>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "lmax_md/MdParse.hpp"
#include "lmax_om/act/LmaxOM.hpp"

namespace lmax::om
{
  bool LmaxOMConfig::from_file(const std::string &path, LmaxOMConfig &c, en::x &venue, std::string &err)
  {
    try
    {
      boost::property_tree::ptree pt;
      boost::property_tree::read_ini(path, pt);
      const std::string v = pt.get<std::string>("venue", "");
      bool found = false;
      for (auto &[code, name] : en::x_name_hash)
        if (v == name)
        {
          venue = code;
          found = true;
        }
      if (!found)
        return err = "venue '" + v + "' is not an en::x code (LMAX has none yet: README, Venue code)", false;
      auto &p = c.session.params;
      if (!fix::parse_env(pt.get<std::string>("environment", ""), p.env))
        return err = "environment must be loopback, demo or uat", false;
      p.host = pt.get<std::string>("host", "");
      p.port = pt.get<int>("port", 443);
      p.tls = pt.get<bool>("tls", true);
      p.heartbeat_s = pt.get<int>("heartbeat_s", 30);
      p.reconnect_s = pt.get<int>("reconnect_s", 5);
      p.dictionary = pt.get<std::string>("dictionary", "");
      p.store_path = pt.get<std::string>("store_path", "");
      p.log_path = pt.get<std::string>("log_path", "");
      // Sequence numbers persist for the week; LMAX resets them on Saturday
      // at 17:02 New York time (Broker Trading spec, Session Resetting).
      p.reset_on_logon = pt.get<bool>("reset_on_logon", false);
      p.start_day = pt.get<std::string>("start_day", "Sat");
      p.end_day = pt.get<std::string>("end_day", "Sat");
      p.start_time = pt.get<std::string>("start_time", "17:02:00");
      p.end_time = pt.get<std::string>("end_time", "17:01:00");
      if (p.store_path.empty() && !p.reset_on_logon)
        return err = "store_path is required unless reset_on_logon (sequence numbers persist for the week)", false;
      c.reinstate_on_disconnect = pt.get<bool>("reinstate_on_disconnect", false);
      c.status_on_logon = pt.get<bool>("status_on_logon", true);
      c.max_order_qty = pt.get<int>("max_order_qty", 0);
      c.max_orders_per_sec = pt.get<int>("max_orders_per_sec", 0);
      std::vector<lmax::md::Instrument> ins;
      if (!lmax::md::load_instruments(pt.get<std::string>("instruments", ""), ins, err))
        return false;
      for (auto &i : ins)
        c.instruments[i.sec_id] = OmInstrument{i.security_id, i.symbol, i.tick, i.chunk};
      if (p.env == fix::Env::Loopback)
      {
        c.session.username = pt.get<std::string>("username", "");
        c.session.password = pt.get<std::string>("password", "");
        p.sender = pt.get<std::string>("sender_comp_id", "");
        p.target = pt.get<std::string>("target_comp_id", "");
        c.account = pt.get<std::string>("account", "");
      }
      else
      {
        fix::Credentials cr;
        if (!fix::load_credentials(pt.get<std::string>("key_file", ""), cr, err))
          return false;
        if (cr.env != p.env)
          return err = std::string("key file is for ") + fix::to_string(cr.env) + ", config for " +
                       fix::to_string(p.env),
                 false;
        c.session.username = cr.username;
        c.session.password = cr.password;
        p.sender = cr.trade_sender;
        p.target = cr.trade_target;
        c.account = cr.account;
      }
      std::string text;
      return fix::settings_text(p, text, err); // the host guard
    }
    catch (const std::exception &e)
    {
      return err = path + ": " + e.what(), false;
    }
  }
}

actors::Actor *create_LmaxOM(const std::string &config_path, actors::Actor *som, actors::Actor *trade_sink)
{
  lmax::om::LmaxOMConfig cfg;
  en::x venue;
  std::string err;
  if (!lmax::om::LmaxOMConfig::from_file(config_path, cfg, venue, err))
  {
    std::cerr << "create_LmaxOM: " << err << std::endl;
    return nullptr;
  }
  auto factory = lmax::om::qf_session_factory(cfg.session);
  return new lmax::om::LmaxOM(std::move(cfg), som, std::move(factory), venue,
                              frame::som::ClientIdGen::from_clock(), trade_sink);
}
