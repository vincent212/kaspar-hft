#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

#include <boost/property_tree/ptree.hpp>

#include "actors/Actor.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/msg/Start.hpp"
#include "okx_md/OkxSync.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace okx::md
{
  /**
   * OkxFeed -- live OKX V5 public market data for one instrument (spot or
   * swap), as MBO records to that instrument's OB. NOT CERTIFIED.
   *
   *   WebSocket (TLS)  wss://ws.okx.com/ws/v5/public, channels `books`
   *                    (400 levels, 100 ms) and `trades`; no API key
   *        -> OkxSync  (seqId/prevSeqId, checksum field, trades by seqId)
   *        -> Mbp2Mbo  (the best window_depth levels -> chunk orders)
   *        -> frame::mda::msg::Data to the OB
   *
   * Runs its own read loop, as the sim/ feeds do. Silence for 20 s sends the
   * text "ping" (OKX closes a connection after 30 s without data); a gap,
   * an error event or a dropped connection reconnects after a second, and
   * the snapshot on the new subscription reaches OB as recovery records.
   *
   * With record_path set, every frame is also written to that file in the
   * okx_record format ("W <recv_ns> <frame>").
   *
   * With no OB it runs as a monitor, printing the top of the book once a
   * second.
   */
  class OkxFeed : public actors::Actor
  {
  public:
    struct Config
    {
      std::string inst_id;                 // e.g. BTC-USDT, BTC-USDT-SWAP
      std::string host = "ws.okx.com";     // demo: wspap.okx.com
      size_t window_depth = 50;            // levels per side sent to OB
      std::string record_path;             // empty: no recording
      sim::Mbp2Mbo::Config chunks;
    };

    OkxFeed(const Config &cfg, actors::Actor *ob);

    // One instrument's section of okx.ini (config/okx_example.ini).
    static Config config_from_ini(const boost::property_tree::ptree &section, char venue);

    const OkxSync &sync() const { return sync_; }

  private:
    Config cfg_;
    actors::Actor *ob_;
    sim::Mbp2Mbo mbp2mbo_;
    OkxSync sync_;
    std::unique_ptr<std::ofstream> rec_;
    uint64_t records_ = 0;
    uint64_t last_dump_ns_ = 0;

    void start_handler(const actors::msg::Start *) noexcept;
    void continue_handler(const actors::msg::Continue *) noexcept;

    void emit(const bfile::l3_t &r);
    void run_connection(); // throws on error
    void dump(uint64_t now_ns);
  };
}
