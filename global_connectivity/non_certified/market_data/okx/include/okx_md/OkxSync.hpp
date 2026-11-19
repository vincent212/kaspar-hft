#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "sim/LevelWindow.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace okx::md
{
  /**
   * OkxSync -- one OKX V5 instrument's `books` (400 levels) and `trades`
   * channels into Mbp2Mbo. No network: the feed actor, the verifier and the
   * tests hand it each parsed WebSocket message. See feed.md.
   *
   *   books, action "snapshot"  the top 400 levels per side; replaces ours.
   *                             prevSeqId is -1.
   *   books, action "update"    absolute sizes per price; size "0" removes the
   *                             level. Levels that leave the top 400 arrive as
   *                             deletes, so the book never holds more than 400
   *                             per side (checked live, feed.md section 6).
   *   trades                    one message per taker order, price and source;
   *                             `side` is the TAKER's side, `count` the number
   *                             of maker fills aggregated, `tradeId` the last.
   *
   * Sequencing (feed.md section 5): seqId and prevSeqId are per instrument.
   * Every update's prevSeqId must equal the seqId of the message before it
   * (snapshot or update); otherwise it is a gap and on_message returns Gap,
   * the caller resubscribes, and the new snapshot reaches OB as recovery
   * records. A heartbeat update has empty sides and seqId == prevSeqId. After
   * exchange maintenance seqId may be smaller than prevSeqId; the
   * prevSeqId rule still holds and is all that is checked.
   *
   * Checksum: OKX deprecated the `checksum` field on 2026-06-23; it is
   * always 0. The handler still checks it on every message: a non-zero value
   * is compared with CRC32 over the top 25 levels (the documented string
   * format, checksum_string()), and a mismatch is treated as a gap.
   *
   * Only the best `window_depth` levels per side reach Mbp2Mbo (LevelWindow).
   *
   * Whenever a synchronised book is invalidated (a sequence gap, a checksum
   * mismatch, reset() after a lost connection) on_gap is called once: the
   * owner sends l3_gap_v2_t to the OB, whose book is stale until the next
   * snapshot reaches it as recovery records.
   *
   * Trades and the book are ordered by seqId: a trade whose seqId is above
   * the last book seqId applied is not yet in the book (its chunks leave the
   * front of the queue, and the next update brings the level to its size);
   * one at or below it is already in the book. The trade is handed to Mbp2Mbo
   * with a transactTime that makes Mbp2Mbo take the same decision (its rule is
   * by transactTime; feed.md section 7).
   */
  class OkxSync
  {
  public:
    enum class Status
    {
      Ok,      // applied (or nothing to apply)
      Ignored, // not for this instrument or channel, or before the first snapshot
      Gap,     // sequence break or checksum mismatch: resubscribe
    };

    struct Level
    {
      std::string px, sz; // as received, for the checksum string
      double qty = 0;
    };
    // tick -> level. Bids: iterate in reverse for best first.
    using Side = std::map<int64_t, Level>;

    struct Stats
    {
      uint64_t snapshots = 0, updates = 0, heartbeats = 0, seq_resets = 0, gaps = 0;
      uint64_t checksum_zero = 0, checksum_checked = 0, checksum_mismatch = 0;
      uint64_t crossed = 0;             // updates after which best bid >= best ask
      uint64_t trade_msgs = 0, trades = 0, trade_id_gaps = 0;
      uint64_t trades_before_book = 0;  // trade seqId above the last book seqId
      uint64_t trades_in_book = 0;      // trade seqId at or below it
      size_t max_bids = 0, max_asks = 0;
    };

    OkxSync(std::string inst_id, sim::Mbp2Mbo &out, size_t window_depth);

    Status on_message(const nlohmann::json &msg);

    // A new connection or subscription: wait for a snapshot (on_gap if the
    // book was synced).
    void reset();

    // Called once each time a synchronised book is invalidated.
    void on_gap(std::function<void()> f) { on_gap_ = std::move(f); }

    bool synced() const { return synced_; }
    int64_t seq_id() const { return seq_; }
    uint64_t book_ts_ms() const { return book_ts_ms_; }
    const Side &bids() const { return bids_; }
    const Side &asks() const { return asks_; }
    const Stats &stats() const { return st_; }
    const std::string &inst_id() const { return inst_; }

    // "bidPx:bidSz:askPx:askSz:..." over the best `levels` per side, as
    // received; a side with fewer levels is skipped where it ends.
    std::string checksum_string(size_t levels = 25) const;
    // CRC32 (IEEE 802.3, as zlib) as a signed 32-bit integer.
    static int32_t crc32(std::string_view s);
    int32_t checksum() const { return crc32(checksum_string()); }

    // Called after every book message applied, with the message's seqId.
    std::function<void(int64_t seq)> on_book;

  private:
    std::string inst_;
    sim::Mbp2Mbo &out_;
    sim::LevelWindow window_;
    Side bids_, asks_;
    bool synced_ = false;
    bool started_ = false;
    int64_t seq_ = -1;
    uint64_t book_ts_ms_ = 0;
    uint64_t last_book_tx_ = 0;
    int64_t last_trade_id_ = -1;
    Stats st_;
    std::function<void()> on_gap_;

    void invalidate();
    Status on_books(const nlohmann::json &d, bool snapshot);
    void on_trade(const nlohmann::json &d);
    void set(bool bid, const nlohmann::json &lvl);
  };
}
