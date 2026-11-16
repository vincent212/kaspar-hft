#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

#include "bfile/r_l3.hpp"

namespace sim
{
  /**
   * Mbp2Mbo -- turns a price-level feed into per-order (MBO) records for OB.
   *
   * OB fills our orders by queue position, which needs individual orders.
   * Binance publishes levels: a price and the total quantity there. Mbp2Mbo
   * splits each level into synthetic orders of one fixed size, the CHUNK, and
   * keeps them in a FIFO per level:
   *
   *   level grows      -> new chunks join the back of the queue
   *   level shrinks    -> chunks leave from the back (a cancel; the newest
   *                       liquidity is the likeliest to be pulled)
   *   trade at a level -> chunks leave from the front: for each, an execution
   *                       record (l3_mbo_trd_v2_t) and then the delete of that
   *                       order, both with the trade's transactTime
   *
   * Every chunk has displayQty 1, so OB counts size in chunks and our own
   * orders must be sized in chunks too.
   *
   * A level holding quantity q has ceil(q / chunk) chunks. Depth updates are
   * absolute, so applying one after a trade cannot double-count: the trade has
   * already removed its chunks, and the update only brings the count to the
   * new target.
   *
   * Trades are fractional: a trade of less than a chunk is accumulated per side
   * and a chunk executes each time the running total reaches the chunk size.
   *
   * Prices go out in ticks (pxd = price / tick, an integer), so the asset's
   * units must be 1.
   *
   * Records of one event are buffered and released by end_event(), the last one
   * flagged endOfEvent and followed by an l3_eob_t, which is what drives the
   * lights' end-of-burst processing.
   */
  class Mbp2Mbo
  {
  public:
    struct Config
    {
      int32_t sec_id = 0;   // the asset's securityID; OB drops records for others
      char venue = 0;       // en::x value
      double tick = 0;      // price increment
      double chunk = 0;     // quantity per synthetic order
    };

    using Emit = std::function<void(const bfile::l3_t &)>;

    Mbp2Mbo(const Config &cfg, Emit emit);

    // The trading-status record OB needs first: status 17 (matching). Without
    // it OB treats a start inside CME's 17:00-18:00 ET break as pre-open.
    void start(uint64_t tx);

    // Absolute quantity at one level. qty 0 removes the level.
    void level(bool bid, double px, double qty, uint64_t tx);

    // A trade. `buyer_is_maker` true means the resting order was a bid.
    void trade(double px, double qty, bool buyer_is_maker, uint64_t tx);

    // Replace the whole book: clear it and re-add every level as recovery
    // records, which make OB clear its own book first.
    void snapshot(const std::vector<std::pair<double, double>> &bids,
                  const std::vector<std::pair<double, double>> &asks, uint64_t tx);

    // Release the buffered records of the current event.
    void end_event();

    // Inspection, for tests and the --dump mode.
    int64_t ticks(double px) const;
    size_t chunks(bool bid, int64_t ticks) const;
    const std::deque<uint64_t> *queue(bool bid, int64_t ticks) const;
    int64_t best(bool bid) const;   // 0 when the side is empty
    uint64_t last_tx() const { return last_tx_; }

  private:
    using Book = std::map<int64_t, std::deque<uint64_t>>;

    Config cfg_;
    Emit emit_;
    Book bids_, asks_;
    double acc_bid_ = 0, acc_ask_ = 0;   // fractional trade quantity per side
    uint64_t next_oid_ = 1;
    uint64_t last_tx_ = 0;
    uint64_t event_tx_ = 0;              // transactTime of the open event
    std::vector<bfile::l3_t> pending_;

    uint64_t stamp(uint64_t tx);
    size_t target(double qty) const;
    void push_mbo(uint8_t action, bool bid, int64_t t, uint64_t oid, uint64_t tx,
                  bool recovery = false);
    void push_exec(uint64_t oid, uint64_t tx);
    void set_level(Book &book, bool bid, int64_t t, size_t n, uint64_t tx,
                   bool recovery = false);
  };
}
