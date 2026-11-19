#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Dispatcher: decoded ITCH and GLIMPSE messages to Kaspar L3 records, as
// feed.md section 7 says. It keeps the order map (Order Number -> order), the
// handler's own book, the current Timestamp - Seconds, and the trading status of
// every book. Sequencing and recovery are the Handler's.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "jnx_md/Instruments.hpp"
#include "jnx_md/Types.hpp"
#include "jnx_md/gen/glimpse.hpp"
#include "jnx_md/gen/itch.hpp"

namespace jnx_md
{
  class Dispatcher
  {
  public:
    struct Config
    {
      char venue = char(en::x::JNX);
      unsigned stream = 0;          // MoldUDP64 stream index, 0..63: orderID bits 58..63
      uint64_t midnight_ns = 0;     // UTC epoch ns of JST midnight of the session date
      bool check_cross = true;      // count crossed books at each timestamp boundary
    };

    Dispatcher(const Config &cfg, Instruments &inst, Sink &sink, Stats &stats)
        : cfg_(cfg), inst_(inst), sink_(sink), st_(stats)
    {
    }

    // One live ITCH message, with its MoldUDP64 sequence number.
    void on_itch(const jnx::itch::from_jnx::message_t &m, uint64_t seq);
    // One GLIMPSE message other than End of Snapshot.
    void on_glimpse(const jnx::glimpse::from_jnx::message_t &m);
    // Before the first message of a MoldUDP64 packet: its first sequence number.
    void packet(uint64_t first_seq) { pkt_ = first_seq; }
    // After the last message of a MoldUDP64 packet.
    void end_of_packet(uint64_t first_seq);

    // Channel reset: l3_chr_v2_t to every book, then clear every per-channel
    // state (order map, books, seconds, tick tables, listing, states).
    void reset();
    // A GLIMPSE snapshot starts (after reset()) / has ended: the market-hours
    // phase of every group then comes from the clock at the snapshot time
    // (decision 5).
    void begin_snapshot();
    void end_snapshot(bool clock = true);
    // A gap that recovery has not closed: l3_gap_v2_t to every book.
    void gap();
    // Forget the current seconds (after preloading a directory from a capture
    // taken at another time of day).
    void forget_time()
    {
      have_secs_ = false;
      secs_ = 0;
      day_add_ = 0;
      last_ns_ = 0;
      last_time_ = 0;
    }
    void set_midnight(uint64_t ns) { cfg_.midnight_ns = ns; }

    // State, for tests, the replay tool and the snapshot writer.
    const std::unordered_map<uint64_t, Order> &orders() const { return orders_; }
    uint32_t seconds() const { return secs_ + day_add_; }
    uint32_t last_nanos() const { return last_ns_; }
    uint64_t now() const { return time_of(last_ns_); }
    uint8_t status_of(const Instrument &i) const;
    Phase phase_of(const std::string &group) const;
    bool in_snapshot() const { return snapshot_; }
    const Config &config() const { return cfg_; }

  private:
    struct GroupPhase
    {
      Phase phase = Phase::Unknown;
      bool from_event = false;   // set by a System Event Q or M
    };

    Config cfg_;
    Instruments &inst_;
    Sink &sink_;
    Stats &st_;
    std::unordered_map<uint64_t, Order> orders_;
    std::map<std::string, GroupPhase> phase_;
    bool have_secs_ = false;
    uint32_t secs_ = 0, day_add_ = 0, last_ns_ = 0;
    bool snapshot_ = false;
    uint64_t seq_ = 0;             // sequence number of the message being handled
    uint64_t pkt_ = 0;             // first sequence number of its packet
    uint64_t last_time_ = 0;       // transactTime of the previous message
    std::unordered_set<Instrument *> touched_;   // books changed at last_time_

    template <class M>
    void handle(const M &m);

    uint64_t time_of(uint32_t ns) const
    {
      return cfg_.midnight_ns + uint64_t(secs_ + day_add_) * 1000000000ull + ns;
    }
    uint64_t stamp(uint32_t ns);
    void seconds(uint32_t s);
    void check_crossed();
    void touch(Instrument *i, uint64_t t);

    void send_status(Instrument &i, uint64_t t);
    void group_status(const std::string &group, uint64_t t);
    void clock_phases(uint64_t t);

    void add(Instrument &i, uint64_t number, char side, uint32_t qty, uint32_t px,
             char order_type, uint64_t t);
    void execute(uint64_t number, uint32_t qty, uint64_t t);
    void remove(uint64_t number, uint64_t t);
    void replace(uint64_t original, uint64_t number, uint32_t qty, uint32_t px, uint64_t t);
    void check_price(const Instrument &i, uint32_t px);

    bfile::l3_mbo_v2_t mbo(const Order &o, uint8_t action, uint64_t t) const;
    void emit(int32_t asset, const bfile::l3_t &r);
  };
}
