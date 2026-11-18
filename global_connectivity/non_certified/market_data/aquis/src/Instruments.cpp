/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "aquis/Instruments.hpp"

#include <algorithm>
#include <fstream>

namespace aquis
{

std::vector<std::string> split_csv(const std::string &line)
{
  std::vector<std::string> out;
  std::string cur;
  bool quoted = false;
  for (size_t i = 0; i < line.size(); ++i)
  {
    const char c = line[i];
    if (quoted)
    {
      if (c == '"' && i + 1 < line.size() && line[i + 1] == '"')
        cur += '"', ++i;
      else if (c == '"')
        quoted = false;
      else
        cur += c;
    }
    else if (c == '"')
      quoted = true;
    else if (c == ',')
      out.push_back(cur), cur.clear();
    else if (c != '\r')
      cur += c;
  }
  out.push_back(cur);
  return out;
}

static std::string trim(std::string s)
{
  // A UTF-8 byte-order mark starts Aquis's public CSV files.
  if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
      (unsigned char)s[2] == 0xBF)
    s.erase(0, 3);
  const auto b = s.find_first_not_of(" \t");
  const auto e = s.find_last_not_of(" \t");
  return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

void Instruments::add_universe(const std::string &key, int32_t sec_id)
{
  universe_[key] = sec_id;
  for (auto &[id, ins] : by_id_)
    if (ins.umtf == key)
      ins.sec_id = sec_id;
}

int Instruments::load_universe(const std::string &path, std::string *err)
{
  std::ifstream f(path);
  if (!f)
  {
    if (err)
      *err = "cannot open " + path;
    return -1;
  }
  std::string line;
  int n = 0, lineno = 0;
  while (std::getline(f, line))
  {
    ++lineno;
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    auto c = split_csv(line);
    if (c.size() < 2)
    {
      if (err)
        *err = path + ":" + std::to_string(lineno) + ": expected umtf,sec_id";
      return -1;
    }
    try
    {
      add_universe(trim(c[0]), std::stoi(trim(c[1])));
    }
    catch (...)
    {
      if (err)
        *err = path + ":" + std::to_string(lineno) + ": bad sec_id";
      return -1;
    }
    ++n;
  }
  return n;
}

int32_t Instruments::universe_id(const std::string &umtf) const
{
  auto it = universe_.find(umtf);
  return umtf.empty() || it == universe_.end() ? -1 : it->second;
}

int Instruments::load_security_file(const std::string &path, const std::string &operating_mic,
                                    std::string *err)
{
  std::ifstream f(path);
  if (!f)
  {
    if (err)
      *err = "cannot open " + path;
    return -1;
  }
  std::string line;
  if (!std::getline(f, line))
  {
    if (err)
      *err = path + ": empty";
    return -1;
  }
  // Columns by header name, so that a reordered or extended file still reads.
  auto hdr = split_csv(line);
  std::map<std::string, size_t> col;
  for (size_t i = 0; i < hdr.size(); ++i)
    col[trim(hdr[i])] = i;
  for (const char *need : {"security_id", "mic", "isin", "umtf", "currency"})
    if (!col.count(need))
    {
      if (err)
        *err = path + ": no column " + need;
      return -1;
    }
  auto get = [&](const std::vector<std::string> &c, const char *name) -> std::string {
    auto it = col.find(name);
    return it == col.end() || it->second >= c.size() ? std::string() : trim(c[it->second]);
  };
  int n = 0, lineno = 1;
  while (std::getline(f, line))
  {
    ++lineno;
    if (trim(line).empty())
      continue;
    auto c = split_csv(line);
    if (!operating_mic.empty() && col.count("operating_mic") && get(c, "operating_mic") != operating_mic)
      continue;
    unsigned long id = 0;
    try
    {
      id = std::stoul(get(c, "security_id"));
    }
    catch (...)
    {
      if (err)
        *err = path + ":" + std::to_string(lineno) + ": bad security_id";
      return -1;
    }
    if (id > 0xFFFF)
    {
      if (err)
        *err = path + ":" + std::to_string(lineno) + ": security_id does not fit u16";
      return -1;
    }
    Instrument &ins = by_id_[uint16_t(id)];
    ins.aquis_id = uint16_t(id);
    ins.umtf = get(c, "umtf");
    ins.isin = get(c, "isin");
    ins.currency = get(c, "currency");
    ins.mic = get(c, "mic");
    ins.tick_table = get(c, "tick table");
    ins.sec_id = universe_id(ins.umtf);
    ++n;
  }
  return n;
}

void Instruments::on_definition(uint16_t aquis_id, const std::string &umtf, const std::string &isin,
                                const std::string &currency, const std::string &mic,
                                uint8_t tick_table_id, uint16_t flags,
                                const std::string &exchange_symbol)
{
  Instrument &ins = by_id_[aquis_id];
  ins.aquis_id = aquis_id;
  ins.umtf = umtf;
  ins.isin = isin;
  ins.currency = currency;
  ins.mic = mic;
  ins.tick_table_id = tick_table_id;
  ins.flags = flags;
  ins.exchange_symbol = exchange_symbol;
  ins.sec_id = universe_id(umtf);
}

void Instruments::on_tick_band(uint8_t table_id, const std::string &, uint64_t threshold, uint64_t tick)
{
  auto &t = tick_tables_[table_id];
  auto it = std::find_if(t.begin(), t.end(), [&](const TickBand &b) { return b.threshold == threshold; });
  if (it != t.end())
    it->tick = tick;
  else
    t.push_back({threshold, tick});
  std::sort(t.begin(), t.end(), [](const TickBand &a, const TickBand &b) { return a.threshold < b.threshold; });
}

std::optional<uint64_t> Instruments::tick(uint16_t aquis_id, uint64_t price) const
{
  const Instrument *ins = find(aquis_id);
  if (!ins)
    return std::nullopt;
  auto it = tick_tables_.find(ins->tick_table_id);
  if (it == tick_tables_.end() || it->second.empty())
    return std::nullopt;
  uint64_t tk = it->second.front().tick;
  for (const auto &b : it->second)
    if (price >= b.threshold)
      tk = b.tick;
  return tk;
}

size_t Instruments::routed() const
{
  return size_t(std::count_if(by_id_.begin(), by_id_.end(), [](const auto &p) { return p.second.sec_id >= 0; }));
}

} // namespace aquis
