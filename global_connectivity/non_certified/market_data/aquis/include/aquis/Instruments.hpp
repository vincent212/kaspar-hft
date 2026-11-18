#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Aquis instrument loader (not certified; see ../../README.md).
//
// Aquis identifies a security by a u16 securityID, unique within one venue
// (Aquis UK or Aquis EU) for the day (feed.md section 8). Definitions come from
// two sources, which this class merges:
//
//  - the daily security file, in the format of Aquis's public SecurityList.csv
//    (company description, security_id, mic, isin, umtf, currency, tick table,
//    operating_mic), loaded before the open;
//  - Security Definition (8) and Tick Table Data (7) messages on the feed,
//    pre-market and, rarely, intraday corrections (MD p.14-16, 3.4.8-3.4.9).
//
// A security is routed to Kaspar only when the universe file maps its UMTF to a
// Kaspar asset id (the `securityID` of every L3 record). The UMTF, not the
// ISIN, is the key: in Aquis's public SecurityList.csv of 2026-11-18, 1,107
// ISINs appear on more than one row (one listing per market of listing and
// currency, each with its own securityID), while no UMTF repeats.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace aquis
{

struct Instrument
{
  uint16_t aquis_id = 0;
  int32_t sec_id = -1;          // Kaspar asset id; -1 when not in the universe
  std::string umtf, isin, currency, mic, tick_table, exchange_symbol;
  uint8_t tick_table_id = 0;    // from Security Definition only
  uint16_t flags = 0;           // Security Definition flags (MD p.15-16, 3.4.9)
};

struct TickBand
{
  uint64_t threshold = 0;       // price, 5 implied decimals
  uint64_t tick = 0;            // price, 5 implied decimals
};

class Instruments
{
public:
  // Universe: CSV lines "umtf,sec_id"; '#'
  // starts a comment. Returns the number of entries, or -1 if the file cannot
  // be read; err says why.
  int load_universe(const std::string &path, std::string *err = nullptr);
  void add_universe(const std::string &key, int32_t sec_id);

  // Aquis security file in the SecurityList.csv format. Returns the number of
  // securities read, or -1; err says why. Rows whose operating_mic is not
  // `operating_mic` are skipped when it is not empty.
  int load_security_file(const std::string &path, const std::string &operating_mic = "",
                         std::string *err = nullptr);

  // From the feed. A definition replaces the one held for that securityID.
  void on_definition(uint16_t aquis_id, const std::string &umtf, const std::string &isin,
                     const std::string &currency, const std::string &mic, uint8_t tick_table_id,
                     uint16_t flags, const std::string &exchange_symbol);
  void on_tick_band(uint8_t table_id, const std::string &name, uint64_t threshold, uint64_t tick);

  // Kaspar asset id for an Aquis securityID; -1 if unknown or not routed.
  int32_t sec_id(uint16_t aquis_id) const
  {
    auto it = by_id_.find(aquis_id);
    return it == by_id_.end() ? -1 : it->second.sec_id;
  }
  const Instrument *find(uint16_t aquis_id) const
  {
    auto it = by_id_.find(aquis_id);
    return it == by_id_.end() ? nullptr : &it->second;
  }
  // Tick size (5 implied decimals) at `price` from the tick table the feed
  // defined for the security; nullopt when the table has not been received.
  std::optional<uint64_t> tick(uint16_t aquis_id, uint64_t price) const;

  size_t size() const { return by_id_.size(); }
  size_t routed() const;

private:
  int32_t universe_id(const std::string &umtf) const;

  std::unordered_map<std::string, int32_t> universe_;
  std::unordered_map<uint16_t, Instrument> by_id_;
  std::map<uint8_t, std::vector<TickBand>> tick_tables_;
};

// Split one CSV line; double-quoted fields may hold commas and "" quotes.
std::vector<std::string> split_csv(const std::string &line);

} // namespace aquis
