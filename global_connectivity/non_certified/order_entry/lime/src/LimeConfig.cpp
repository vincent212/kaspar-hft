/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// LimeOMConfig: reading and checking the order manager's settings. The file
// format is config/lime_om_example.ini. Credentials live in a separate file
// named by `credentials`, kept outside the repository.

#include <sstream>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "lime_om/act/LimeOM.hpp"

namespace lime
{
  bool LimeOMConfig::from_file(const std::string &path, LimeOMConfig &c, en::x &venue, std::string &err)
  {
    try
    {
      boost::property_tree::ptree pt;
      boost::property_tree::read_ini(path, pt);
      auto v = pt.get<std::string>("venue", "LIME");
      bool found = false;
      for (auto &[code, name] : en::x_name_hash)
        if (v == name)
        {
          venue = code;
          found = true;
        }
      if (!found)
        return err = "venue '" + v + "' is not an en::x code", false;
      c.sim = pt.get<bool>("sim", false);
      c.account = pt.get<std::string>("account", "");
      c.host = pt.get<std::string>("host", "");
      c.infiniband = pt.get<bool>("infiniband", false);
      c.cancel_all_on_disconnect = pt.get<bool>("cancel_all_on_disconnect", c.cancel_all_on_disconnect);
      c.first_event_id = pt.get<uint64_t>("first_event_id", c.first_event_id);
      c.reconnect_ms = pt.get<int>("reconnect_ms", c.reconnect_ms);
      c.default_route = pt.get<std::string>("default_route", "");
      c.algo_route = pt.get<std::string>("algo_route", "");
      if (auto s = pt.get_optional<std::string>("algo_strategies"))
      {
        c.algo_strategies.clear();
        std::stringstream ss(*s);
        std::string t;
        while (std::getline(ss, t, ','))
        {
          while (!t.empty() && t.front() == ' ')
            t.erase(t.begin());
          while (!t.empty() && t.back() == ' ')
            t.pop_back();
          if (!t.empty())
            c.algo_strategies.insert(t);
        }
      }
      c.allow_market = pt.get<bool>("allow_market", c.allow_market);
      c.allow_short = pt.get<bool>("allow_short", c.allow_short);
      c.sale_affirm = pt.get<bool>("sale_affirm", c.sale_affirm);
      c.regular_session_only = pt.get<bool>("regular_session_only", c.regular_session_only);
      c.max_order_qty = pt.get<int>("max_order_qty", 0);
      c.max_order_notional = pt.get<double>("max_order_notional", 0);
      c.max_orders_per_sec = pt.get<int>("max_orders_per_sec", 0);
      c.sim_config.ack_delay_ms = pt.get<int>("sim_ack_delay_ms", c.sim_config.ack_delay_ms);
      c.sim_config.fill_delay_ms = pt.get<int>("sim_fill_delay_ms", c.sim_config.fill_delay_ms);
      c.sim_config.fill_parts = pt.get<int>("sim_fill_parts", c.sim_config.fill_parts);
      c.sim_config.fill = pt.get<bool>("sim_fill", c.sim_config.fill);

      auto cred = pt.get<std::string>("credentials", "");
      if (!cred.empty())
      {
        boost::property_tree::ptree cp;
        boost::property_tree::read_ini(cred, cp);
        c.user = cp.get<std::string>("user", "");
        c.password = cp.get<std::string>("password", "");
        if (c.account.empty())
          c.account = cp.get<std::string>("account", "");
      }
    }
    catch (const std::exception &e)
    {
      err = e.what();
      return false;
    }
    return c.valid(err);
  }

  bool LimeOMConfig::valid(std::string &err) const
  {
    if (!sim)
    {
      if (account.empty())
        return err = "account is empty", false;
      if (user.empty())
        return err = "user is empty (credentials file)", false;
      if (password.empty())
        return err = "password is empty (credentials file)", false;
    }
    if (default_route.empty())
      return err = "default_route is empty: every Lime order needs a destination", false;
    for (auto &s : algo_strategies)
      if (s == "POV" || s == "SCALEPOV")
        return err = "strategy " + s + " needs MaxParticipation, which the C++ API cannot send", false;
    if (reconnect_ms < 0 || max_order_qty < 0 || max_order_notional < 0 || max_orders_per_sec < 0)
      return err = "negative limit", false;
    if (sim_config.fill_parts < 1)
      return err = "sim_fill_parts must be at least 1", false;
    return true;
  }
}
