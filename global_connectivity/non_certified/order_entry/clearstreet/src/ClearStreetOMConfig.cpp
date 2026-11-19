/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// ClearStreetOMConfig::from_file and create_ClearStreetOM.

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

#include "clearstreet_om/SimClearStreet.hpp"
#include "clearstreet_om/act/ClearStreetOM.hpp"

namespace clearstreet::om
{
  namespace
  {
    std::string trim(const std::string &s)
    {
      size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
      return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    }

    bool read_kv(const std::string &path, std::map<std::string, std::string> &kv, std::string &err)
    {
      std::ifstream in(path);
      if (!in)
        return err = "cannot read " + path, false;
      std::string line;
      int n = 0;
      while (std::getline(in, line))
      {
        ++n;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
          continue;
        auto eq = line.find('=');
        if (eq == std::string::npos)
          return err = path + ":" + std::to_string(n) + ": expected key = value", false;
        kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
      }
      return true;
    }

    bool yes(const std::string &v) { return v == "yes" || v == "true" || v == "1" || v == "Y"; }

    int to_int(const std::map<std::string, std::string> &kv, const std::string &k, int def)
    {
      auto it = kv.find(k);
      return it == kv.end() || it->second.empty() ? def : std::stoi(it->second);
    }
    std::string get(const std::map<std::string, std::string> &kv, const std::string &k, const std::string &def = "")
    {
      auto it = kv.find(k);
      return it == kv.end() ? def : it->second;
    }
  }

  std::set<std::string> ClearStreetOMConfig::guide_mics()
  {
    // FIX API guide, Execution strategies, "Destination (DMA)".
    return {"XNYS", "XNAS", "XASE", "ARCX", "XCHI", "XCIS", "XBOS", "XPHL", "BATS",
            "BATY", "EDGA", "EDGX", "EPRL", "IEXG", "LTSE", "MEMX", "24EQ", "TXSE"};
  }

  bool ClearStreetOMConfig::from_file(const std::string &path, ClearStreetOMConfig &c, en::x &venue, std::string &err)
  {
    try
    {
      std::map<std::string, std::string> kv;
      if (!read_kv(path, kv, err))
        return false;
      const std::string v = get(kv, "venue", "CLEARSTREET");
      bool found = false;
      for (auto &[code, name] : en::x_name_hash)
        if (v == name)
        {
          venue = code;
          found = true;
        }
      if (!found)
        return err = "venue '" + v + "' is not an en::x code", false;

      c.environment = get(kv, "environment");
      if (c.environment != "sim" && c.environment != "uat" && c.environment != "production")
        return err = "environment must be sim, uat or production", false;
      if (c.environment == "production" && !yes(get(kv, "production_ok")))
        return err = "environment = production needs production_ok = yes", false;

      c.account = get(kv, "account");
      if (c.account.empty())
        return err = "account is required (tag 1 on every order)", false;
      c.clordid_prefix = get(kv, "clordid_prefix");
      for (char ch : c.clordid_prefix)
        if (!std::isalnum((unsigned char)ch))
          return err = "clordid_prefix must be letters and digits", false;
      if (c.clordid_prefix.size() > 20)
        return err = "clordid_prefix longer than 20 characters", false;
      c.mark_short_open = yes(get(kv, "mark_short_open", "yes"));
      c.max_order_qty = to_int(kv, "max_order_qty", 0);
      c.max_notional = std::stod(get(kv, "max_notional", "0"));
      c.max_orders_per_sec = to_int(kv, "max_orders_per_sec", 0);
      c.dma_destinations = guide_mics();

      // instruments: asset_id,symbol,tick per line
      const std::string inst = get(kv, "instruments");
      std::ifstream in(inst);
      if (!in)
        return err = "cannot read instruments file '" + inst + "'", false;
      std::string line;
      while (std::getline(in, line))
      {
        line = trim(line);
        if (line.empty() || line[0] == '#')
          continue;
        std::stringstream ss(line);
        std::string id, sym, tick;
        if (!std::getline(ss, id, ',') || !std::getline(ss, sym, ',') || !std::getline(ss, tick, ','))
          return err = "instruments: expected asset_id,symbol,tick: " + line, false;
        CsInstrument ci{trim(sym), std::stod(trim(tick))};
        if (ci.symbol.empty() || !(ci.tick > 0))
          return err = "instruments: bad line: " + line, false;
        c.instruments[std::stoi(trim(id))] = ci;
      }
      if (c.instruments.empty())
        return err = "no instruments", false;

      if (c.environment == "sim")
        return true;
      auto &s = c.session;
      s.host = get(kv, "host");
      s.port = to_int(kv, "port", 0);
      s.tls = yes(get(kv, "tls", "no"));
      s.sender_comp_id = get(kv, "sender_comp_id");
      s.target_comp_id = get(kv, "target_comp_id");
      s.heartbeat = to_int(kv, "heartbeat", 30);
      s.dictionary = get(kv, "dictionary");
      s.store_path = get(kv, "store_path");
      s.log_path = get(kv, "log_path");
      s.start_day = get(kv, "start_day", s.start_day);
      s.start_time = get(kv, "start_time", s.start_time);
      s.end_day = get(kv, "end_day", s.end_day);
      s.end_time = get(kv, "end_time", s.end_time);
      s.reconnect_interval = to_int(kv, "reconnect_interval", 5);
      for (auto &[k, val] : kv)
        if (k.rfind("qf.", 0) == 0)
          s.extra[k.substr(3)] = val;
      if (s.host.empty() || s.port <= 0 || s.sender_comp_id.empty() || s.target_comp_id.empty())
        return err = "host, port, sender_comp_id and target_comp_id are required", false;
      if (s.store_path.empty())
        return err = "store_path is required", false;
      if (s.dictionary.empty())
        return err = "dictionary is required (make dict)", false;

      // The API key: a file outside the repository, one line, or "api_key = ...".
      const std::string kf = get(kv, "key_file");
      std::ifstream k(kf);
      if (!k)
        return err = "cannot read key_file '" + kf + "'", false;
      while (std::getline(k, line))
      {
        line = trim(line);
        if (line.empty() || line[0] == '#')
          continue;
        auto eq = line.find('=');
        s.api_key = eq == std::string::npos ? line : trim(line.substr(eq + 1));
        break;
      }
      if (s.api_key.empty())
        return err = "key_file has no API key", false;
      return true;
    }
    catch (const std::exception &e)
    {
      err = std::string("bad value: ") + e.what();
      return false;
    }
  }
}

actors::Actor *create_ClearStreetOM(const std::string &config_path, actors::Actor *som)
{
  using namespace clearstreet::om;
  ClearStreetOMConfig cfg;
  en::x venue = en::x::CLEARSTREET;
  std::string err;
  if (!ClearStreetOMConfig::from_file(config_path, cfg, venue, err))
  {
    std::cerr << "create_ClearStreetOM(" << config_path << "): " << err << std::endl;
    return nullptr;
  }
  SessionFactory f;
  if (cfg.environment == "sim")
  {
    SimConfig sc;
    sc.accounts = {cfg.account};
    f = sim_session_factory(sc);
  }
  else
    f = qf_session_factory(cfg.session);
  return new ClearStreetOM(std::move(cfg), som, std::move(f), venue);
}
