/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OuchOMConfig: reading and checking the order manager's settings. The
// file format is config/ouch_example.ini. Credentials live in a separate
// file named by `credentials`, kept outside the repository.

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "nasdaq_ouch/act/OuchOM.hpp"

namespace nasdaq::ouch
{
  namespace
  {
    bool one_of(char c, const char *allowed)
    {
      for (; *allowed; ++allowed)
        if (c == *allowed)
          return true;
      return false;
    }

    char letter(const boost::property_tree::ptree &pt, const char *key, char def)
    {
      auto s = pt.get<std::string>(key, std::string(1, def));
      return s.size() == 1 ? s[0] : '?';
    }
  }

  bool OuchOMConfig::from_file(const std::string &path, OuchOMConfig &c, std::string &err)
  {
    try
    {
      boost::property_tree::ptree pt;
      boost::property_tree::read_ini(path, pt);
      c.sim = pt.get<bool>("sim", false);
      c.session.host = pt.get<std::string>("host", "");
      c.session.port = pt.get<std::string>("port", "");
      c.session.heartbeat_ms = pt.get<int>("heartbeat_ms", c.session.heartbeat_ms);
      c.session.receive_timeout_ms = pt.get<int>("receive_timeout_ms", c.session.receive_timeout_ms);
      c.session.connect_timeout_ms = pt.get<int>("connect_timeout_ms", c.session.connect_timeout_ms);
      c.session.max_queued_bytes = pt.get<size_t>("max_queued_bytes", c.session.max_queued_bytes);
      c.first_sequence = pt.get<uint64_t>("first_sequence", c.first_sequence);
      c.reconnect_ms = pt.get<int>("reconnect_ms", c.reconnect_ms);
      c.firm = pt.get<std::string>("firm", "");
      c.capacity = letter(pt, "capacity", c.capacity);
      c.display = letter(pt, "display", c.display);
      c.intermarket_sweep = letter(pt, "intermarket_sweep", c.intermarket_sweep);
      c.max_order_qty = pt.get<int>("max_order_qty", 0);
      c.max_order_notional = pt.get<double>("max_order_notional", 0);
      c.max_orders_per_sec = pt.get<int>("max_orders_per_sec", 0);
      c.sim_config.fill_delay_ms = pt.get<int>("sim_fill_delay_ms", c.sim_config.fill_delay_ms);
      c.sim_config.fill_parts = pt.get<int>("sim_fill_parts", c.sim_config.fill_parts);
      c.sim_config.fill = pt.get<bool>("sim_fill", c.sim_config.fill);

      auto cred = pt.get<std::string>("credentials", "");
      if (!cred.empty())
      {
        boost::property_tree::ptree cp;
        boost::property_tree::read_ini(cred, cp);
        c.username = cp.get<std::string>("username", "");
        c.password = cp.get<std::string>("password", "");
      }
    }
    catch (const std::exception &e)
    {
      err = e.what();
      return false;
    }
    return c.valid(err);
  }

  bool OuchOMConfig::valid(std::string &err) const
  {
    if (!sim)
    {
      if (session.host.empty() || session.port.empty())
        return err = "host and port are required", false;
      if (username.empty() || username.size() > 6)
        return err = "username must be 1 to 6 characters (SoupBinTCP 3.0 2.3.1)", false;
      if (password.size() > 10)
        return err = "password longer than 10 characters (SoupBinTCP 3.0 2.3.1)", false;
    }
    if (firm.size() > 4)
      return err = "firm longer than 4 characters", false;
    if (!one_of(capacity, "APRO"))
      return err = "capacity must be A, P, R or O", false;
    if (!one_of(display, "YNA"))
      return err = "display must be Y, N or A", false;
    if (!one_of(intermarket_sweep, "YN"))
      return err = "intermarket_sweep must be Y or N", false;
    if (reconnect_ms <= 0)
      return err = "reconnect_ms must be positive", false;
    if (max_order_qty < 0 || max_order_notional < 0 || max_orders_per_sec < 0)
      return err = "limits must not be negative", false;
    return true;
  }
}
