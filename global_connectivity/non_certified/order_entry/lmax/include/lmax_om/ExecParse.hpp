/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LMAX Broker Trading (FIX 4.4): the requests the order manager and the
// post-trade client send, and the reports they read, as plain structs.
// NOT CERTIFIED. Field lists follow LMAX's fix-spec.json for the Broker
// Trading API, FIX.4.4 version (feed.md section 4).
//
//   sent      NewOrderSingle (D), OrderCancelRequest (F),
//             OrderCancelReplaceRequest (G), OrderStatusRequest (H),
//             TradeCaptureReportRequest (AD)
//   received  ExecutionReport (8), OrderCancelReject (9),
//             TradeCaptureReport (AE), TradeCaptureReportRequestAck (AQ)
//
// Builders set MsgType and the body in the spec's field order; the session
// fills the rest of the header. Parsers never throw: a missing required
// field is returned as false with the reason.

#include <cstdint>
#include <string>
#include <vector>

namespace FIX
{
  class Message;
}

namespace lmax::om
{
  struct NewOrderReq
  {
    std::string clordid;     // 11: at most 20 ASCII characters
    std::string security_id; // 48 (22=8)
    std::string account;     // 1: empty = the user's default account
    char side = '1';         // 54: 1 buy, 2 sell
    std::string qty;         // 38: contracts, as text
    char ord_type = '2';     // 40: 1 market, 2 limit, 3 stop, 4 stop limit
    std::string px;          // 44, limit and stop limit
    std::string stop_px;     // 99, stop and stop limit
    char tif = '0';          // 59: 0 day, 1 GTC, 3 IOC, 4 FOK
    bool reinstate = false;  // 18=H: not cancelled on disconnect
    std::string transact_time; // 60, UTCTimestamp
  };

  struct CancelReq
  {
    std::string clordid, orig_clordid, security_id, account, qty, transact_time;
    char side = '1';
  };

  struct ReplaceReq
  {
    std::string clordid, orig_clordid, security_id, account, qty, px, transact_time;
    char side = '1', ord_type = '2', tif = '0';
    bool reinstate = false;
  };

  struct StatusReq
  {
    std::string clordid, security_id, account, req_id;
    char side = '1';
  };

  struct TcrReq
  {
    std::string req_id;   // 568: at most 16 ASCII characters
    std::string account;
    bool subscribe = true; // 263: 1 snapshot + updates, 0 snapshot
    std::string from, to;  // NoDates (580) TransactTime values; to may be empty
  };

  FIX::Message build_new_order(const NewOrderReq &r);
  FIX::Message build_cancel(const CancelReq &r);
  FIX::Message build_replace(const ReplaceReq &r);
  FIX::Message build_status(const StatusReq &r);
  FIX::Message build_tcr_request(const TcrReq &r);

  struct ExecReport
  {
    std::string order_id;          // 37
    std::string secondary_exec_id; // 527: MTF execution id on trades, "0" otherwise
    std::string clordid, orig_clordid, status_req_id;
    std::string exec_id;           // 17: unique over all time
    char exec_type = 0;            // 150
    char ord_status = 0;           // 39
    int rej_reason = -1;           // 103
    std::string account, security_id, symbol;
    char side = 0, ord_type = 0, tif = 0;
    double qty = 0, px = 0, stop_px = 0;
    bool has_last = false;
    double last_qty = 0, last_px = 0; // 32, 31
    std::string last_qty_text, last_px_text; // as sent, for exact decimals
    double leaves = 0, cum = 0, avg_px = 0;
    std::string transact_time, settl_date, text;
    std::string commission, comm_type, comm_ccy;
    uint64_t seq = 0;
    bool poss_dup = false;
    std::string raw; // as received, SOH separators
  };

  struct CancelReject
  {
    std::string order_id, clordid, orig_clordid, account, text;
    char ord_status = 0;
    char response_to = 0; // 434: 1 cancel, 2 cancel/replace
    int reason = -1;      // 102
    uint64_t seq = 0;
    bool poss_dup = false;
  };

  struct TradeCaptureReport
  {
    std::string req_id, exec_id, secondary_exec_id, security_id, symbol;
    bool last_rpt = false;
    double last_qty = 0, last_px = 0;
    std::string last_qty_text, last_px_text; // as sent, for exact decimals
    std::string trade_date, transact_time;
    std::string commission, comm_type, comm_ccy;
    struct Side
    {
      char side = 0;
      std::string order_id, clordid, account;
    };
    std::vector<Side> sides;
    uint64_t seq = 0;
    bool poss_dup = false;
    std::string raw;
  };

  struct TcrAck
  {
    std::string req_id, text;
    int result = -1, status = -1; // 749, 750
    int total = -1;               // 748, set only when no trades matched
    char subscription = 0;
  };

  bool parse_exec(const FIX::Message &m, ExecReport &r, std::string &err);
  bool parse_cancel_reject(const FIX::Message &m, CancelReject &r, std::string &err);
  bool parse_tcr(const FIX::Message &m, TradeCaptureReport &r, std::string &err);
  bool parse_tcr_ack(const FIX::Message &m, TcrAck &r, std::string &err);

  // UTCTimestamp of now, milliseconds.
  std::string utc_now();
  // A decimal with the digits needed for `step` (0.00001 -> 5 places).
  std::string decimal(double v, double step);
  int decimals_of(double step);
}
