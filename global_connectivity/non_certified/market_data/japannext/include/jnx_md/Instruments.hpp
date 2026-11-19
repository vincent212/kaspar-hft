#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Instrument loader: Price Tick Size (L) and Orderbook Directory (R) messages,
// from the ITCH stream at the start of the day or from a GLIMPSE snapshot, to
// Kaspar assets in ref::RefData (feed.md section 8).
//
// One asset per (Orderbook Id, Group). Japanese equities have price-dependent
// ticks; ref::Price and OB assume one tick per instrument. The asset's tick
// (Asset::units) is the greatest common divisor of the ticks of the bands its
// daily price limits reach, which on Japannext's tables is the smallest of them
// (every tick is a multiple of the ones below it). Every price the venue
// accepts for that book is then a whole number of Kaspar ticks, and OB's ladder
// has UpperPriceLimit / tick + 1 slots. The venue's own grid is still checked
// per price (Stats::px_off_tick).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "jnx_md/Book.hpp"
#include "jnx_md/Types.hpp"

namespace jnx_md
{
  struct Instrument
  {
    std::string sicc, isin, group;   // OrderbookId, OrderbookCode, Group (unpadded)
    BookKey key{};
    uint32_t round_lot = 0, table = 0, decimals = 1;
    uint32_t upper = 0, lower = 0;   // daily limits, venue price units
    uint32_t unit = 1;               // venue price units per Kaspar tick
    int32_t asset = -1;              // Kaspar asset id; securityID in every record
    int maxpx = 0;                   // Asset::maxpx, in Kaspar ticks
    bool listed = false;             // an R since the last reset
    char trading_state = 0;          // H: 'T', 'V'; 0 = not in the spin
    char short_sell = '0';           // Y
    uint32_t ref_price = 0;          // A with Order Number 0; 0x7FFFFFFF none
    uint8_t status_sent = 0;         // last tradingStatus sent; 0 = none yet
    Book book;
  };

  class Instruments
  {
  public:
    struct Config
    {
      bool register_assets = true;        // create ref::RefData assets
      std::string name_prefix = "JNX:";   // asset name: prefix + SICC + '.' + group
      int32_t secid_base = 81000000;      // Asset::sec_id = base + directory index
    };

    explicit Instruments(const Config &cfg) : cfg_(cfg) {}

    // A Price Tick Size row (L): tick and start in venue price units.
    void tick_row(uint32_t table, uint32_t tick, uint32_t start);

    // An Orderbook Directory message (R). Returns the instrument, created on
    // its first R. An instrument keeps its asset across resets: OB books are
    // bound to assets.
    Instrument *directory(const std::string &sicc, const std::string &isin,
                          const std::string &group, const BookKey &key,
                          uint32_t round_lot, uint32_t table, uint32_t decimals,
                          uint32_t upper, uint32_t lower);

    Instrument *find(const BookKey &k)
    {
      auto it = by_key_.find(k);
      return it == by_key_.end() ? nullptr : it->second;
    }
    Instrument *by_asset(int32_t asset)
    {
      auto it = by_asset_.find(asset);
      return it == by_asset_.end() ? nullptr : it->second;
    }

    // The venue's tick for a price under a table: the row with the highest
    // start below the price (a boundary price belongs to the lower band,
    // feed.md section 10). 0 if the table is unknown.
    uint32_t tick_at(uint32_t table, uint32_t px) const;

    // The Kaspar tick chosen for a book (see the comment at the top).
    uint32_t unit_for(uint32_t table, uint32_t upper, uint32_t lower) const;

    const std::map<uint32_t, std::map<uint32_t, uint32_t>> &tables() const { return tables_; }
    const std::vector<std::unique_ptr<Instrument>> &all() const { return all_; }
    const std::vector<Instrument *> &group(const std::string &g) const;
    const std::map<std::string, std::vector<Instrument *>> &groups() const { return groups_; }

    // Channel reset: tick tables, listing, states and books are cleared; the
    // instruments and their assets stay.
    void reset();

    uint64_t unit_changes() const { return unit_changes_; }

  private:
    Config cfg_;
    std::map<uint32_t, std::map<uint32_t, uint32_t>> tables_;   // table -> start -> tick
    std::vector<std::unique_ptr<Instrument>> all_;
    std::map<BookKey, Instrument *> by_key_;
    std::unordered_map<int32_t, Instrument *> by_asset_;
    std::map<std::string, std::vector<Instrument *>> groups_;
    uint64_t unit_changes_ = 0;

    void register_asset(Instrument &i);
  };
}
