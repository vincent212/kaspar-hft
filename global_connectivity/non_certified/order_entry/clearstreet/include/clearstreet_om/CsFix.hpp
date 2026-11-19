/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Clear Street FIX API (FIX 4.2): the requests the order manager sends and the
// reports it reads, as plain structs. NOT CERTIFIED. Field lists follow
// Clear Street's public FIX API guide (docs.clearstreet.com/guides/fix-api,
// retrieved 2026-11-18; feed.md section 4).
//
//   sent      NewOrderSingle (D), OrderCancelRequest (F),
//             OrderCancelReplaceRequest (G)
//   received  ExecutionReport (8), OrderCancelReject (9), Reject (3)
//
// Builders set MsgType and the body in the order of the guide's examples; the
// session fills the header. Parsers never throw: a missing required field is
// returned as false with the reason.

#include <cstdint>
#include <string>

namespace FIX
{
  class Message;
}

namespace clearstreet::om
{
  struct NewOrderReq
  {
    std::string account;      // 1, required
    std::string clordid;      // 11: 1-64 printable ASCII, unique forever
    std::string symbol;       // 55: case-sensitive US equity ticker
    char side = '1';          // 54: 1 buy, 2 sell (5 and 6 are rejected by Clear Street)
    char open_close = 0;      // 77: 'O' or 'C'; 0 = omit (Clear Street decides)
    std::string qty;          // 38: decimal > 0
    char ord_type = '2';      // 40: 1 market, 2 limit, 3 stop, 4 stop limit
    std::string px;           // 44: limit and stop limit only
    std::string stop_px;      // 99: stop and stop limit only
    char tif = '0';           // 59: 0 day, 1 GTC, 2 OPG, 3 IOC, 4 FOK, 7 at the close
    int target_strategy = 0;  // 847: 1001 VWAP, 1002 TWAP, 1003 DMA; 0 = omit
    std::string ex_destination; // 100: MIC, DMA only
    bool extended_hours = false; // 336=6
    std::string transact_time;   // 60
  };

  struct ReplaceReq : NewOrderReq
  {
    std::string orig_clordid; // 41: the order's current ClOrdID
  };

  struct CancelReq
  {
    std::string account, orig_clordid, clordid, symbol, qty, transact_time;
    char side = '1';
  };

  FIX::Message build_new_order(const NewOrderReq &r);
  FIX::Message build_replace(const ReplaceReq &r);
  FIX::Message build_cancel(const CancelReq &r);

  struct ExecReport
  {
    std::string order_id;      // 37: stable for the order; "NONE" if rejected before one was assigned
    std::string exec_id;       // 17: unique per report
    char exec_trans_type = '0'; // 20: always 0 in this version of the API
    char exec_type = 0;        // 150: A 0 1 2 3 4 5 8 D
    char ord_status = 0;       // 39
    std::string clordid;       // 11: the order's current ClOrdID
    std::string orig_clordid;  // 41: on a confirmed cancel or replace; absent on unsolicited cancels
    std::string account, symbol;
    char side = 0;
    double qty = 0, px = 0, stop_px = 0;
    double last_px = 0, last_qty = 0; // 31, 32: 0 on non-fill reports
    double leaves = 0, cum = 0, avg_px = 0;
    int rej_reason = -1;        // 103: 3 exceeds limit, 6 duplicate
    std::string restatement;    // 378
    std::string transact_time, effective_time, text;
    uint64_t seq = 0;
    bool poss_dup = false;
    std::string raw;
  };

  struct CancelReject
  {
    std::string order_id, clordid, orig_clordid, account, text;
    char ord_status = 0;
    char response_to = 0; // 434: 1 cancel, 2 cancel/replace
    int reason = -1;      // 102: 0 too late, 1 unknown order, 3 pending
    uint64_t seq = 0;
    bool poss_dup = false;
  };

  // A session-level Reject (35=3). Clear Street uses it for some order
  // refusals (an unknown or unauthorized Account on 35=D, guide "Submitting
  // an order"), so the session resolves RefSeqNum to the request it sent.
  struct SessionReject
  {
    uint64_t ref_seq = 0;      // 45
    std::string ref_msg_type;  // 372, or the type of the request we sent with that number
    int ref_tag = -1;          // 371
    int reason = -1;           // 373
    std::string text;          // 58
    std::string clordid;       // ClOrdID of the request with MsgSeqNum ref_seq; empty if none
  };

  bool parse_exec(const FIX::Message &m, ExecReport &r, std::string &err);
  bool parse_cancel_reject(const FIX::Message &m, CancelReject &r, std::string &err);
  bool parse_session_reject(const FIX::Message &m, SessionReject &r, std::string &err);

  // UTCTimestamp of now, milliseconds: YYYYMMDD-HH:MM:SS.sss
  std::string utc_now();
  // Nanoseconds since the epoch of a FIX UTCTimestamp; 0 if it does not parse.
  uint64_t utc_ns(const std::string &ts);
  // Today's UTC date as YYYYMMDD.
  std::string utc_date();
  // ticks x tick as a plain fixed-point decimal with the digits `tick` needs
  // (0.01 -> 2 places, 0.0001 -> 4); never scientific notation.
  std::string decimal(int64_t ticks, double tick);
  int decimals_of(double tick);
}
