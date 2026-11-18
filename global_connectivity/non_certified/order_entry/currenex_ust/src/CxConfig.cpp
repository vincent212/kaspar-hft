/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// CurrenexOMConfig: reading and checking the order manager's settings. The
// file format is config/currenex_ust_example.ini. Credentials live in a
// separate file named by `credentials`, kept outside the repository.

#include <cmath>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "currenex_ust/act/CurrenexOM.hpp"

namespace currenex::ouch
{
  bool CurrenexOMConfig::from_file(const std::string &path, CurrenexOMConfig &c, en::x &venue, std::string &err)
  {
    try
    {
      boost::property_tree::ptree pt;
      boost::property_tree::read_ini(path, pt);
      auto v = pt.get<std::string>("venue", "");
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
      c.session.host = pt.get<std::string>("host", "");
      c.session.port = pt.get<std::string>("port", "");
      c.session.receive_timeout_ms = pt.get<int>("receive_timeout_ms", c.session.receive_timeout_ms);
      c.session.connect_timeout_ms = pt.get<int>("connect_timeout_ms", c.session.connect_timeout_ms);
      c.session.logon_timeout_ms = pt.get<int>("logon_timeout_ms", c.session.logon_timeout_ms);
      c.session.max_queued_bytes = pt.get<size_t>("max_queued_bytes", c.session.max_queued_bytes);
      c.version = pt.get<int16_t>("version", c.version);
      c.reconnect_ms = pt.get<int>("reconnect_ms", c.reconnect_ms);
      c.persistence = pt.get<bool>("persistence", c.persistence);
      c.cancel_on_disconnect = pt.get<bool>("cancel_on_disconnect", c.cancel_on_disconnect);
      c.first_resend_from = pt.get<uint32_t>("first_resend_from", c.first_resend_from);
      c.routed_user_key = pt.get<int32_t>("routed_user_key", c.routed_user_key);
      c.routed_user_id = pt.get<std::string>("routed_user_id", "");
      c.face_per_unit = pt.get<double>("face_per_unit", c.face_per_unit);
      c.min_amt = pt.get<double>("min_amt", c.min_amt);
      c.max_order_face = pt.get<double>("max_order_face", 0);
      c.max_orders_per_sec = pt.get<int>("max_orders_per_sec", 0);
      c.sim_config.fill_delay_ms = pt.get<int>("sim_fill_delay_ms", c.sim_config.fill_delay_ms);
      c.sim_config.fill_parts = pt.get<int>("sim_fill_parts", c.sim_config.fill_parts);
      c.sim_config.fill = pt.get<bool>("sim_fill", c.sim_config.fill);
      c.sim_config.pending_fill = pt.get<bool>("sim_pending_fill", c.sim_config.pending_fill);

      auto cred = pt.get<std::string>("credentials", "");
      if (!cred.empty())
      {
        boost::property_tree::ptree cp;
        boost::property_tree::read_ini(cred, cp);
        c.user = cp.get<std::string>("user", "");
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

  bool CurrenexOMConfig::valid(std::string &err) const
  {
    if (!sim)
    {
      if (session.host.empty() || session.port.empty())
        return err = "host and port are required", false;
      if (user.empty() || user.size() > 20)
        return err = "user must be 1 to 20 characters (UST OUCH 10.1)", false;
      if (password.size() > 20)
        return err = "password longer than 20 characters (UST OUCH 10.1)", false;
    }
    if (version != VERSION)
      return err = "version must be 200 (UST OUCH 10.1; 300 is the Firm LP protocol)", false;
    if (routed_user_id.size() > 20)
      return err = "routed_user_id longer than 20 characters (UST OUCH 10.8)", false;
    if (!(face_per_unit >= 0.01) || face_per_unit * AMOUNT_SCALE != std::floor(face_per_unit * AMOUNT_SCALE))
      return err = "face_per_unit must be a positive number of cents", false;
    if (min_amt < 0)
      return err = "min_amt must not be negative", false;
    if (reconnect_ms <= 0)
      return err = "reconnect_ms must be positive", false;
    if (max_order_face < 0 || max_orders_per_sec < 0)
      return err = "limits must not be negative", false;
    return true;
  }
}
