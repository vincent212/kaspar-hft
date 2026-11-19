#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <memory>
#include <string>

#include <boost/property_tree/ptree.hpp>

#include "actors/Actor.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/msg/Start.hpp"
#include "kraken_md/FrameRecorder.hpp"
#include "kraken_md/KrakenBookSync.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace kraken
{
  /**
   * KrakenFeed -- live Kraken spot market data for one symbol, as MBO records
   * to that symbol's OB. Not certified (README.md).
   *
   *   WebSocket (TLS)  wss://ws.kraken.com/v2, channels `book` (depth 10 to
   *                    1000) and `trade` (no snapshot); no API key needed
   *        -> KrakenBookSync (snapshot, updates, truncation, CRC32 check,
   *                           trade_id continuity)
   *        -> LevelWindow    (the best `window` levels)
   *        -> Mbp2Mbo        (levels -> chunk orders)
   *        -> frame::mda::msg::Data to the OB
   *
   * Runs its own thread, as sim's CoinbaseFeed does. A checksum mismatch, an
   * unreadable frame or a dropped connection reconnects after a second; the
   * new subscription starts with a snapshot, which Mbp2Mbo sends to OB as
   * recovery records. With `record` set, every frame is written to that file
   * (FrameRecorder) before it is applied.
   *
   * With no OB it runs as a monitor, printing the top of the book once a second.
   */
  class KrakenFeed : public actors::Actor
  {
  public:
    struct Config
    {
      std::string symbol = "BTC/USD";
      int depth = 1000;   // book subscription depth: 10, 25, 100, 500, 1000
      int window = 1000;  // levels per side passed to Mbp2Mbo
      std::string host = "ws.kraken.com";
      std::string record; // raw-frame file; empty = none
      sim::Mbp2Mbo::Config chunks;
    };

    KrakenFeed(const Config &cfg, actors::Actor *ob);

    // A symbol's section of kraken.ini: tick, chunk, sec_id, depth, window, record.
    static Config config_from_ini(const boost::property_tree::ptree &section, const std::string &symbol, char venue);

    const KrakenBookSync &sync() const { return sync_; }
    uint64_t reconnects() const { return reconnects_; }

  private:
    Config cfg_;
    actors::Actor *ob_;
    sim::Mbp2Mbo mbp2mbo_;
    KrakenBookSync sync_;
    std::unique_ptr<FrameRecorder> recorder_;
    uint64_t records_ = 0, reconnects_ = 0;
    uint64_t last_dump_ns_ = 0;

    void start_handler(const actors::msg::Start *) noexcept;
    void continue_handler(const actors::msg::Continue *) noexcept;
    void emit(const bfile::l3_t &r);
    void run_connection();
    void dump(uint64_t now_ns);
  };
}
