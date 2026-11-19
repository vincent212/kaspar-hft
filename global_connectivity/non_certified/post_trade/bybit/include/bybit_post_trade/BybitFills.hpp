#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Bybit V5 executions into post_trade::msg::TradeCapture (feed.md section 7).
// NOT CERTIFIED. No network: the client's sources hand it one element of the
// private `execution` topic's data array, or of GET /v5/execution/list's
// result.list; both have the same fields, except that a REST element has no
// `category` (it is the request's).
//
//   trade_id = report_id = execId     (stable; the same on the stream and REST)
//   report_seq = 0                    (Bybit's `seq` is shared by simultaneous
//                                      fills and across symbols: arrival order)
//   event = New                       for every execution that moves a position:
//                                      Trade, AdlTrade, BustTrade (a takeover
//                                      liquidation, NOT a trade bust),
//                                      Delivery, BlockTrade, MovePosition,
//                                      FutureSpread; Bybit V5 has no amendment,
//                                      cancellation or bust of an execution
//   Funding, Settle                   cash, not trades: not captured
//   price = execPrice as sent (mantissa, decimals); qty = execQty / qty_unit
//   trade_date, trade_time_ns         execTime (ms) in UTC: Bybit's day
//   venue_order_id = orderId; account = orderLinkId (the OM's client id)

#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "enum/exch_code.hpp"
#include "post_trade/msg/TradeCapture.hpp"
#include "post_trade/Decimal.hpp"

namespace bybit_pt
{
  struct Instrument
  {
    std::string category; // spot | linear
    std::string symbol;   // BTCUSDT
    int asset = -1;       // Kaspar asset id
    double qty_unit = 0;  // quantity of one TradeCapture qty unit (qtyStep, basePrecision)
  };
  using Instruments = std::map<std::pair<std::string, std::string>, Instrument>; // (category, symbol)

  enum class ExecKind
  {
    Position, // a trade: captured
    Cash,     // Funding, Settle: not captured
    Unknown,  // a type Bybit added after this was written: logged, not captured
  };
  ExecKind classify(const std::string &exec_type);

  en::x venue_of(const std::string &category); // spot BYBIT, linear BYBITF, else invalid

  using post_trade::parse_decimal; // post_trade/Decimal.hpp

  enum class Parse
  {
    Ok,
    NotATrade,     // Cash or Unknown execType
    NoInstrument,  // (category, symbol) not configured: a position nobody maps
    BadQuantity,   // execQty not a whole number of qty_unit
    Malformed,     // a field missing or unparsable
  };
  const char *to_string(Parse p);

  // e: one execution; category: e["category"] if present, else this.
  Parse to_capture(const nlohmann::json &e, const std::string &category, const Instruments &ins,
                   bool from_rest, post_trade::msg::TradeCapture &out, std::string &why);
}
