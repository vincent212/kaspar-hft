#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "sim/LevelWindow.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace sim
{
  /**
   * CoinbaseSync -- Coinbase Advanced Trade market data (the `level2`,
   * `market_trades` and `heartbeats` channels of
   * wss://advanced-trade-ws.coinbase.com) into Mbp2Mbo. No network: the feed
   * actor hands it each parsed message.
   *
   *   l2_data, event "snapshot"   the whole book; replaces ours (a Mbp2Mbo
   *                               snapshot of the best `depth` levels)
   *   l2_data, event "update"     absolute quantities, side "bid" or "offer",
   *                               new_quantity 0 removes the level
   *   market_trades, "update"     trades; `side` is the MAKER's side (a SELL
   *                               prints at the ask), so BUY means the bid
   *                               was hit. The "snapshot" event replays recent
   *                               trades and is skipped.
   *
   * Every message on a connection carries sequence_num, counting up by one
   * across all channels. A jump is a gap: on_message returns false and the
   * feed reconnects, and the new subscription starts with a new snapshot.
   *
   * Trades in one message come newest first; they are applied oldest first.
   */
  class CoinbaseSync
  {
  public:
    CoinbaseSync(const std::string &product, Mbp2Mbo &out, size_t depth)
        : product_(product), out_(out), window_(out, depth)
    {
    }

    // One WebSocket message. false = sequence gap; reconnect.
    bool on_message(const nlohmann::json &msg);

    // A new connection: no sequence yet, wait for a snapshot.
    void reset()
    {
      prev_seq_ = -1;
      synced_ = false;
    }

    // "2026-11-17T13:59:46.893523Z" -> ns since the epoch (UTC). 0 if malformed.
    static uint64_t parse_time(const std::string &s);

    bool synced() const { return synced_; }
    uint64_t gaps() const { return gaps_; }
    uint64_t events_applied() const { return applied_; }
    uint64_t trades_applied() const { return trades_; }

  private:
    std::string product_;
    Mbp2Mbo &out_;
    LevelWindow window_;
    int64_t prev_seq_ = -1;
    bool synced_ = false;
    bool started_ = false;
    uint64_t gaps_ = 0, applied_ = 0, trades_ = 0;

    void on_book(const nlohmann::json &ev, uint64_t msg_tx);
    void on_trades(const nlohmann::json &ev);
  };
}
