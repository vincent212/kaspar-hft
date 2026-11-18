/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LMAX Broker Market Data (FIX 4.4): the two application messages the handler
// sends and reads, as plain structs. NOT CERTIFIED.
//
//   MarketDataRequest (V)              built by md_request()
//   MarketDataSnapshotFullRefresh (W)  read by parse_snapshot()
//   MarketDataRequestReject (Y)        read by parse_md_reject()
//
// LMAX sends no MarketDataIncrementalRefresh (X): its Broker Market Data spec
// lists V, W and Y only, and W is the whole book (up to 20 levels a side) every
// time (feed.md sections 4 and 6).

#include <cstdint>
#include <string>
#include <vector>

namespace FIX
{
  class Message;
}

namespace lmax::md
{
  // One instrument the handler subscribes to (config/instruments.csv).
  struct Instrument
  {
    std::string security_id; // LMAX SecurityID (48), SecurityIDSource 8: "4001" is EUR/USD
    std::string symbol;      // LMAX symbol, for logs: "EUR/USD"
    int32_t sec_id = 0;      // Kaspar asset id: the securityID of every record
    double tick = 0;         // price increment (LMAX price_increment)
    double chunk = 0;        // MDEntrySize per synthetic order (feed.md section 8)
  };

  // security_id,symbol,sec_id,tick,chunk; '#' starts a comment.
  bool load_instruments(const std::string &path, std::vector<Instrument> &out, std::string &err);

  struct MdEntry
  {
    char type = 0;       // 269: '0' bid, '1' offer, '2' trade
    double px = 0;       // 270
    double size = 0;     // 271
    bool has_px = false, has_size = false;
    std::string date;    // 272 UTCDateOnly, as sent
    std::string time;    // 273 UTCTimeOnly, as sent
  };

  struct MdSnapshot
  {
    std::string md_req_id;   // 262
    std::string security_id; // 48
    uint64_t seq = 0;        // MsgSeqNum (34)
    uint64_t sending_ns = 0; // SendingTime (52), ns since the epoch
    std::vector<MdEntry> entries; // in message order
  };

  // W into s. false (reason in err) if a required field is missing or bad.
  bool parse_snapshot(const FIX::Message &m, MdSnapshot &s, std::string &err);

  struct MdReject
  {
    std::string md_req_id;
    char reason = 0; // 281
    std::string text;
  };
  bool parse_md_reject(const FIX::Message &m, MdReject &r, std::string &err);

  // MDReqID (262): at most 16 lowercase hex digits, no leading zero, not 0
  // (spec, MarketDataRequest). n must be non-zero.
  std::string md_req_id(uint64_t n);
  bool valid_md_req_id(const std::string &s);

  // MarketDataRequest for one instrument: SubscriptionRequestType 1 (snapshot
  // + updates) or 2 (unsubscribe), MarketDepth `depth` (1..20, 0 = full,
  // which LMAX caps at 20), MDUpdateType 0, AggregatedBook Y, entry types
  // bid and offer, and trade when `trades`. The header's MsgType is set; the
  // session fills the rest of the header.
  FIX::Message md_request(const std::string &req_id, const std::string &security_id, int depth, bool trades,
                          bool unsubscribe = false);
}
