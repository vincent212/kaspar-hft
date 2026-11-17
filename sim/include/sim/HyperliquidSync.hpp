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
   * HyperliquidSync -- Hyperliquid perpetuals market data (the `l2Book` and
   * `trades` subscriptions of wss://api.hyperliquid.xyz/ws) into Mbp2Mbo. No
   * network: the feed actor hands it each parsed message.
   *
   *   l2Book   a snapshot of the best 20 levels per side, sent whole every
   *            time; diffed against the last one, so Mbp2Mbo sees only the
   *            levels that changed. A level that drops out of the 20 leaves
   *            the book; it comes back at the back of the queue if it
   *            re-enters.
   *   trades   `side` is the TAKER's side: "A" sold into the bid, "B" bought
   *            from the ask. On subscribe Hyperliquid replays recent trades;
   *            those older than the first book are skipped.
   *
   * There is no sequence number: each book is complete, so a lost message
   * costs nothing but the levels it would have changed until the next one.
   */
  class HyperliquidSync
  {
  public:
    HyperliquidSync(const std::string &coin, Mbp2Mbo &out, size_t depth = 20)
        : coin_(coin), out_(out), window_(out, depth)
    {
    }

    // One WebSocket message: {"channel": ..., "data": ...}.
    void on_message(const nlohmann::json &msg);

    // A new connection: the next book is a snapshot.
    void reset() { synced_ = false; }

    bool synced() const { return synced_; }
    uint64_t events_applied() const { return applied_; }
    uint64_t trades_applied() const { return trades_; }

  private:
    std::string coin_;
    Mbp2Mbo &out_;
    LevelWindow window_;
    bool synced_ = false;
    bool started_ = false;
    uint64_t first_book_ns_ = 0;   // trades before this are the subscribe replay
    uint64_t applied_ = 0, trades_ = 0;

    void on_book(const nlohmann::json &data);
    void on_trades(const nlohmann::json &data);
  };
}
