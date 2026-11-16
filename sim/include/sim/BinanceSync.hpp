#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <deque>

#include <nlohmann/json.hpp>

#include "sim/Mbp2Mbo.hpp"

namespace sim
{
  /**
   * BinanceSync -- keeps a local order book in step with Binance's diff depth
   * stream and feeds it, with the trades, to Mbp2Mbo. No network: the feed
   * actor hands it parsed messages and REST snapshots.
   *
   * Binance's procedure for a local book
   * (https://developers.binance.com/docs/binance-spot-api-docs/web-socket-streams,
   *  https://developers.binance.com/docs/derivatives/usds-margined-futures/websocket-market-streams/How-to-manage-a-local-order-book-correctly):
   *
   *   1. buffer the depth events;
   *   2. fetch a REST snapshot, lastUpdateId L;
   *   3. drop buffered events older than the snapshot
   *        spot:    u <= L          futures: u < L
   *   4. the first event applied must straddle it
   *        spot:    U <= L+1 <= u   futures: U <= L <= u
   *   5. every later event must follow the previous one
   *        spot:    U == prev_u + 1 futures: pu == prev_u
   *   6. quantities are absolute; 0 removes the level.
   *
   * A break in 4 or 5 is a gap: the book is resynchronised from a new snapshot,
   * which Mbp2Mbo sends to OB as recovery records.
   *
   * Trades are applied only while the book is in sync. Spot sends `trade`,
   * futures `aggTrade`; both carry p, q, m (buyer is maker) and T (trade time).
   */
  class BinanceSync
  {
  public:
    enum class Market { SPOT, FUTURES };

    BinanceSync(Market market, Mbp2Mbo &out) : market_(market), out_(out) {}

    // One stream message: the "data" object of a combined-stream message.
    void on_message(const nlohmann::json &data);

    // A REST depth snapshot.
    void on_snapshot(const nlohmann::json &snap, uint64_t now_ns);

    // A new connection: forget the stream position and wait for a snapshot.
    void reset()
    {
      buffer_.clear();
      synced_ = false;
      need_snapshot_ = true;
      prev_u_ = -1;
    }

    bool need_snapshot() const { return need_snapshot_; }
    bool synced() const { return synced_; }
    uint64_t gaps() const { return gaps_; }
    uint64_t events_applied() const { return applied_; }
    uint64_t trades_applied() const { return trades_; }

  private:
    Market market_;
    Mbp2Mbo &out_;
    std::deque<nlohmann::json> buffer_;
    bool need_snapshot_ = true;
    bool synced_ = false;
    bool started_ = false;
    int64_t last_update_id_ = 0;   // L of the snapshot in force
    int64_t prev_u_ = -1;          // -1: no event applied since the snapshot
    uint64_t gaps_ = 0, applied_ = 0, trades_ = 0;

    // false = a gap: the event does not follow what was applied.
    bool apply_depth(const nlohmann::json &ev);
    void apply_trade(const nlohmann::json &ev);
    void gap(const nlohmann::json &ev);
  };
}
