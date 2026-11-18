#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Aquis Exchange market data handler core (not certified; see ../../README.md).
//
// One instance per real-time market-split feed (one sequence stream). It takes
// raw UDP payloads of the real-time feed (A and B both: duplicates are dropped
// by sequence number), of the matching snapshot feed, and the byte stream of the
// replay TCP connection, and emits Kaspar L3 records as feed.md section 7 says:
//
//   packet reader   1-byte message count, then messages walked by the header's
//                   length (MD p.10-11, 3.2, 3.3.1)
//   dispatcher      generated decoder (aquis_md.tbl), then Kaspar records
//   sequencer       one sequence per feed, from 1 each day; heartbeats carry
//                   the next expected number and are not sequenced (MD p.8, 2.1)
//   recovery        replay request for a gap; if it is refused, too large or
//                   the connection fails: clear the books and rebuild from the
//                   next complete snapshot, then apply the buffered stream
//                   after its streamSeqNo (MD p.8-9, 2.2-2.3; p.19, 3.7.1)
//
// No sockets and no actor here: the caller feeds bytes in and routes records out
// (AquisFeed does both for Kaspar). The handler is single threaded.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aquis/Instruments.hpp"
#include "bfile/r_l3.hpp"

namespace aquis
{

struct HandlerConfig
{
  char venue = 0;                    // en::x::AQXE or en::x::AQEU
  // Start of day: expect seqNo 1 and replay anything missed before the first
  // message. Otherwise (a late start) build the book from the first complete
  // snapshot.
  bool start_of_day = true;
  bool use_replay = true;            // false: every gap goes to the snapshot
  uint32_t max_replay = 50000;       // larger gaps go to the snapshot
  // MD Bundle Start and End (34, 35) consume sequence numbers like every
  // business message (review.md, decision 2). false: they are treated like
  // heartbeats and not sequenced.
  bool bundles_sequenced = true;
};

class Handler
{
public:
  static constexpr int32_t ALL_BOOKS = -1; // every book this feed routes to
  static constexpr int32_t RECORDER = -2;  // end-of-packet record: recorder only

  // sec: the Kaspar securityID of the book the record is for, or ALL_BOOKS, or
  // RECORDER.
  using Emit = std::function<void(int32_t sec, const bfile::l3_t &)>;
  // Ask the replay service for [begin, end] (MD p.21, 3.8.2).
  using RequestReplay = std::function<void(uint32_t begin, uint32_t end)>;

  enum class State
  {
    live,      // applying the real-time stream
    replay,    // a gap is open; replay requested; the stream is buffered
    snapshot,  // books cleared; waiting for a complete snapshot; buffered
  };

  struct Stats
  {
    uint64_t packets = 0, messages = 0, heartbeats = 0, duplicates = 0, gaps = 0;
    uint64_t replay_requests = 0, replay_refused = 0, snapshots_applied = 0, snapshots_discarded = 0;
    uint64_t malformed_packets = 0, count_mismatch = 0, unknown = 0, short_messages = 0;
    uint64_t unknown_orders = 0, overfills = 0, busts = 0, not_routed = 0;
    uint64_t same_size_modifies = 0, hidden_trades = 0, orderref_on_other_trade = 0;
  };

  Handler(const HandlerConfig &cfg, Instruments &ins, Emit emit, RequestReplay request);

  void on_packet(const char *p, size_t n);           // real-time multicast payload
  void on_snapshot_packet(const char *p, size_t n);  // snapshot multicast payload
  void on_replay_data(const char *p, size_t n);      // replay TCP bytes, any split
  void on_replay_lost();                             // replay connection failed or closed

  State state() const { return state_; }
  uint32_t expected() const { return expected_; }
  const Stats &stats() const { return stats_; }
  size_t open_orders() const { return orders_.size(); }
  size_t buffered() const { return buffer_.size(); }

  // CME SecurityTradingStatus for an Aquis tradingStatus and marketFlags
  // (feed.md section 7, trading status).
  static uint8_t kaspar_status(uint8_t trading_status, uint8_t market_flags);

private:
  struct Order
  {
    int32_t sec;
    char side;       // '0' buy, '1' sell
    uint64_t price;  // 5 implied decimals
    uint32_t qty;
  };

  struct SnapBook
  {
    uint16_t id = 0;
    uint8_t status = 0, flags = 0;
    uint16_t remaining = 0;
    struct Entry
    {
      uint8_t side;
      uint32_t qty;
      uint64_t price;
      uint32_t ref;
    };
    std::vector<Entry> entries;
  };

  struct Snap
  {
    bool active = false;
    uint32_t stream_seq = 0;
    uint16_t count = 0;
    uint64_t ts = 0;
    std::vector<SnapBook> books;
  };

  void message(const char *p, size_t len, uint32_t seq, bool replayed);
  void heartbeat(uint32_t seq);
  void open_gap(uint32_t first_missing, uint32_t last_missing);
  void drain();
  void go_snapshot();
  void snapshot_message(const char *p, size_t len);
  void apply_snapshot();
  void apply(const char *p, size_t len, uint32_t seq);

  uint64_t stamp(uint64_t ts);
  void end_event();
  void push(int32_t sec, const bfile::l3_t &r) { pending_.emplace_back(sec, r); }
  void push_mbo(int32_t sec, uint8_t action, uint64_t oid, const Order &o, uint64_t tx, uint64_t st,
                uint64_t prio, bool recovery);

  HandlerConfig cfg_;
  Instruments &ins_;
  Emit emit_;
  RequestReplay request_;

  State state_ = State::live;
  uint32_t expected_ = 1;
  uint32_t highest_seen_ = 0;         // highest sequence number seen on any path
  uint32_t requested_to_ = 0;         // end of the outstanding replay request
  std::map<uint32_t, std::string> buffer_;
  std::string replay_buf_;
  Snap snap_;

  std::unordered_map<uint32_t, Order> orders_;
  std::vector<std::pair<int32_t, bfile::l3_t>> pending_;
  bool in_bundle_ = false;
  uint64_t bundle_tx_ = 0, last_tx_ = 0;
  uint32_t pkt_seq_ = 0;
  uint64_t pkt_send_ = 0;
  bool pkt_has_seq_ = false;

  Stats stats_;
};

// Replay session requests, encoded through the generated replay::to_aquis
// (MD p.20-21, 3.8.1-3.8.2). Return the bytes to write on the TCP connection.
std::string encode_login(const std::string &user, const std::string &password);
std::string encode_replay_request(uint32_t begin, uint32_t end);

} // namespace aquis
