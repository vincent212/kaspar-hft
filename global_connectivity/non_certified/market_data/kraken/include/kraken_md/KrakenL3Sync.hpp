#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "bfile/r_l3.hpp"

namespace kraken
{
  /**
   * KrakenL3Sync -- Kraken spot WebSocket v2 `level3` (order by order) for one
   * symbol, into Kaspar MBO records (feed.md sections 5 to 7). No network.
   *
   * NOT CHECKED LIVE: `level3` needs an API token (an account). This class is
   * built from the documentation and tested on its examples and on synthetic
   * sequences only.
   *
   *   snapshot  every order, in queue order per level; replaces the book.
   *             Sent to OB as recovery adds.
   *   add       a new order joins the back of its level             -> action 0
   *   modify    quantity changed ("i.e. a fill"); a price change or a
   *             quantity increase moves the order to the back (Kraken's
   *             amend rule, guides/general/amends), which is OB's rule
   *                                                                 -> action 1
   *   delete    filled or cancelled                                 -> action 2
   *   after each message the book is truncated to the subscribed depth (no
   *   delete is sent for orders pushed out of scope): their deletes go to OB.
   *   Then the CRC32 over the top 10 levels, order by order in queue order,
   *   is checked.
   *
   * Kraken order ids are 17-character text (e.g. OTCFZG-YOE2Q-LQKNM3), more
   * than 64 bits; each live order gets a handler id from a counter, starting
   * at 1, kept until its delete. The map is cleared on reset and snapshot.
   *
   * Trades come from the public `trade` channel on another connection
   * (trade()). Each trade prints an l3_mbp_trd_t and leaves an execution
   * budget at (resting side, price); a following modify-down or delete of an
   * order on that side at that price, within 1 s, consumes the budget and is
   * preceded by an l3_mbo_trd_v2_t. A book change that arrives before its
   * trade is a cancel for OB. (feed.md section 10 records this.)
   *
   * One message is one event: records are released by the end of the message,
   * the last flagged endOfEvent and followed by an l3_eob_t.
   */
  class KrakenL3Sync
  {
  public:
    struct Config
    {
      int32_t sec_id = 0;
      char venue = 0;          // en::x value
      double tick = 0;         // price increment; pxd = price / tick
      double qty_unit = 1e-8;  // displayQty = ceil(qty / qty_unit)
      size_t depth = 10;       // subscribed depth: 10, 100 or 1000
    };
    enum class Result { Ok, ChecksumMismatch, Error };
    struct Stats
    {
      uint64_t snapshots = 0, updates = 0, adds = 0, modifies = 0, deletes = 0;
      uint64_t moved_to_back = 0, unknown_order = 0, duplicate_add = 0, truncated = 0;
      uint64_t checksum_ok = 0, checksum_bad = 0, crossed = 0, errors = 0;
      uint64_t trades = 0, executions = 0;
    };
    struct Order
    {
      std::string id, px, qty; // text as sent
      int64_t qty_fp = 0;
      uint64_t kid = 0;        // Kaspar orderID
    };
    struct Level
    {
      std::list<Order> q;      // queue order: front first
    };
    using Side = std::map<int64_t, Level>;
    using Emit = std::function<void(const bfile::l3_t &)>;

    KrakenL3Sync(const Config &cfg, std::string symbol, Emit emit);

    Result on_message(const nlohmann::json &msg);
    // A public trade: price and quantity text, taker side, time (ns).
    void trade(const std::string &px, const std::string &qty, bool taker_buy, uint64_t tx);
    void reset();

    uint32_t checksum() const;
    const Side &bids() const { return bids_; }
    const Side &asks() const { return asks_; }
    const Stats &stats() const { return st_; }
    bool synced() const { return synced_; }
    size_t orders() const { return index_.size(); }
    uint64_t kid(const std::string &order_id) const; // 0 if not live

  private:
    struct Where
    {
      bool bid;
      int64_t px;
      std::list<Order>::iterator it;
    };
    struct Budget
    {
      int64_t qty_fp = 0;
      uint64_t tx = 0;
    };

    Config cfg_;
    std::string symbol_;
    Emit emit_;
    Side bids_, asks_;
    std::unordered_map<std::string, Where> index_;
    std::map<std::pair<bool, int64_t>, Budget> budget_;
    std::vector<bfile::l3_t> pending_;
    uint64_t next_kid_ = 1;
    uint64_t last_tx_ = 0, event_tx_ = 0;
    bool synced_ = false, started_ = false;
    Stats st_;

    Result on_data(const nlohmann::json &d, bool snapshot);
    void apply(bool bid, const nlohmann::json &e, bool snapshot);
    void erase(const std::string &id, uint64_t tx, int64_t executed_fp);
    void truncate(bool bid, uint64_t tx);
    int64_t consume(bool bid, int64_t px, int64_t reduction, uint64_t tx);
    uint64_t stamp(uint64_t tx);
    void push_mbo(uint8_t action, bool bid, int64_t px, const Order &o, uint64_t tx, bool recovery);
    void push_exec(uint64_t kid, int64_t qty_fp, uint64_t tx);
    void flush();
    uint32_t units(int64_t qty_fp) const;
  };
}
