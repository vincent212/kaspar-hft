#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// DeribitFeed: live Deribit public market data (JSON-RPC over WebSocket) for
// one instrument, as MBO records to that instrument's OB. NOT CERTIFIED.
//
//   wss://www.deribit.com/ws/api/v2, public/subscribe
//     book.<instrument>.<interval> and trades.<instrument>.<interval>
//     -> DeribitBookSync (snapshot, change_id chain)
//     -> sim::LevelWindow (best `depth` levels of the full book)
//     -> sim::Mbp2Mbo (levels -> chunk orders)
//     -> frame::mda::msg::Data to the OB
//
// Runs its own thread from Continue, as sim/ CoinbaseFeed does. A change_id gap
// or a dropped connection reconnects after a second; the new subscription
// starts with a full snapshot, which Mbp2Mbo sends as recovery records.
// With record_path set, every frame received is appended to that file
// (FrameRecorder) before it is parsed.
//
// With no OB it runs as a monitor, printing the top of the book once a second.

#include <cstdint>
#include <memory>
#include <string>

#include <boost/property_tree/ptree.hpp>

#include "actors/Actor.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/msg/Start.hpp"
#include "deribit_md/DeribitBookSync.hpp"
#include "deribit_md/FrameRecorder.hpp"
#include "sim/Mbp2Mbo.hpp"

namespace deribit
{
  class DeribitFeed : public actors::Actor
  {
  public:
    struct Config
    {
      std::string instrument;          // e.g. BTC-PERPETUAL
      std::string interval = "100ms";  // raw needs an authenticated connection
      std::string host = "www.deribit.com";
      int depth = 50;                  // levels per side passed to Mbp2Mbo
      int heartbeat_s = 30;
      std::string record_path;         // empty: do not record
      sim::Mbp2Mbo::Config chunks;     // tick, chunk (USD for perpetuals), sec_id, venue
    };

    DeribitFeed(const Config &cfg, actors::Actor *ob);

    // An instrument's section of deribit.ini (README.md).
    static Config config_from_ini(const boost::property_tree::ptree &section,
                                  const std::string &instrument, char venue);

    const DeribitBookSync &sync() const { return sync_; }

  private:
    Config cfg_;
    actors::Actor *ob_;
    sim::Mbp2Mbo mbp2mbo_;
    DeribitBookSync sync_;
    std::unique_ptr<FrameRecorder> rec_;
    uint64_t records_ = 0;
    uint64_t last_dump_ns_ = 0;
    int64_t next_id_ = 1;

    void start_handler(const actors::msg::Start *) noexcept;
    void continue_handler(const actors::msg::Continue *) noexcept;
    void emit(const bfile::l3_t &r);
    void run_connection();   // throws on error or a change_id gap
    void dump(uint64_t now_ns);
  };
}
