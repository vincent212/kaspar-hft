/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_md/MdParse.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "quickfix/Message.h"

#include "lmax_fix/LmaxFix.hpp"

namespace lmax::md
{
  namespace
  {
    bool get(const FIX::FieldMap &m, int tag, std::string &v)
    {
      if (!m.isSetField(tag))
        return false;
      v = m.getField(tag);
      return true;
    }

    bool number(const std::string &s, double &v)
    {
      if (s.empty())
        return false;
      char *end = nullptr;
      v = std::strtod(s.c_str(), &end);
      return end && *end == 0;
    }

    std::string trim(const std::string &s)
    {
      size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
      return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }
  }

  bool load_instruments(const std::string &path, std::vector<Instrument> &out, std::string &err)
  {
    std::ifstream in(path);
    if (!in)
      return err = "cannot open " + path, false;
    std::string line;
    int n = 0;
    while (std::getline(in, line))
    {
      ++n;
      if (auto h = line.find('#'); h != std::string::npos)
        line.resize(h);
      if (trim(line).empty())
        continue;
      std::vector<std::string> f;
      std::stringstream ss(line);
      std::string x;
      while (std::getline(ss, x, ','))
        f.push_back(trim(x));
      if (f.size() != 5)
        return err = path + ":" + std::to_string(n) + ": want security_id,symbol,sec_id,tick,chunk", false;
      Instrument i;
      i.security_id = f[0];
      i.symbol = f[1];
      i.sec_id = std::atoi(f[2].c_str());
      if (!number(f[3], i.tick) || !number(f[4], i.chunk) || i.tick <= 0 || i.chunk <= 0 || i.sec_id <= 0 ||
          i.security_id.empty())
        return err = path + ":" + std::to_string(n) + ": bad value", false;
      out.push_back(i);
    }
    if (out.empty())
      return err = path + ": no instruments", false;
    return true;
  }

  bool parse_snapshot(const FIX::Message &m, MdSnapshot &s, std::string &err)
  {
    s = MdSnapshot{};
    const auto &h = m.getHeader();
    std::string v;
    if (!get(h, 35, v) || v != "W")
      return err = "not a MarketDataSnapshotFullRefresh", false;
    if (get(h, 34, v))
      s.seq = std::strtoull(v.c_str(), nullptr, 10);
    if (get(h, 52, v))
      s.sending_ns = fix::utc_timestamp_ns(v);
    get(m, 262, s.md_req_id);
    if (!get(m, 48, s.security_id))
      return err = "W without SecurityID (48)", false;
    if (!m.isSetField(268))
      return err = "W without NoMDEntries (268)", false;
    const int n = std::atoi(m.getField(268).c_str());
    s.entries.reserve(n);
    for (int i = 1; i <= n; ++i)
    {
      FIX::Group g(268, 269);
      try
      {
        m.getGroup(i, g);
      }
      catch (const std::exception &)
      {
        return err = "NoMDEntries says " + std::to_string(n) + ", entry " + std::to_string(i) + " missing", false;
      }
      MdEntry e;
      if (!get(g, 269, v) || v.size() != 1)
        return err = "entry " + std::to_string(i) + " without MDEntryType", false;
      e.type = v[0];
      if (get(g, 270, v))
      {
        if (!number(v, e.px))
          return err = "entry " + std::to_string(i) + ": bad MDEntryPx '" + v + "'", false;
        e.has_px = true;
      }
      if (get(g, 271, v))
      {
        if (!number(v, e.size))
          return err = "entry " + std::to_string(i) + ": bad MDEntrySize '" + v + "'", false;
        e.has_size = true;
      }
      get(g, 272, e.date);
      get(g, 273, e.time);
      s.entries.push_back(std::move(e));
    }
    return true;
  }

  bool parse_md_reject(const FIX::Message &m, MdReject &r, std::string &err)
  {
    r = MdReject{};
    std::string v;
    if (!get(m.getHeader(), 35, v) || v != "Y")
      return err = "not a MarketDataRequestReject", false;
    get(m, 262, r.md_req_id);
    if (get(m, 281, v) && !v.empty())
      r.reason = v[0];
    get(m, 58, r.text);
    return true;
  }

  std::string md_req_id(uint64_t n)
  {
    static const char *hex = "0123456789abcdef";
    std::string s;
    do
    {
      s.insert(s.begin(), hex[n & 15]);
      n >>= 4;
    } while (n);
    return s;
  }

  bool valid_md_req_id(const std::string &s)
  {
    if (s.empty() || s.size() > 16 || s[0] == '0')
      return false;
    for (char c : s)
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        return false;
    return true;
  }

  FIX::Message md_request(const std::string &req_id, const std::string &security_id, int depth, bool trades,
                          bool unsubscribe)
  {
    // Body fields in the spec's order (QuickFIX would otherwise sort them by tag).
    static const int body[] = {262, 263, 264, 265, 266, 267, 146, 0};
    FIX::Message m{FIX::message_order(FIX::message_order::header), FIX::message_order(FIX::message_order::trailer),
                   FIX::message_order(body)};
    m.getHeader().setField(8, "FIX.4.4");
    m.getHeader().setField(35, "V");
    m.setField(262, req_id);
    m.setField(263, unsubscribe ? "2" : "1");
    m.setField(264, std::to_string(depth));
    m.setField(265, "0");
    m.setField(266, "Y");
    const char *types[] = {"0", "1", "2"};
    for (int i = 0; i < (trades ? 3 : 2); ++i)
    {
      int order[] = {269, 0};
      FIX::Group g(267, 269, order);
      g.setField(269, types[i]);
      m.addGroup(g);
    }
    int order[] = {48, 22, 0};
    FIX::Group sym(146, 48, order);
    sym.setField(48, security_id);
    sym.setField(22, "8");
    m.addGroup(sym);
    return m;
  }
}
