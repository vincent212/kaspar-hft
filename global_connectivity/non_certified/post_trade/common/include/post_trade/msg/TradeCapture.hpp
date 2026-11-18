/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// TradeCapture: one post-trade event (a new trade, an amendment, a cancellation
// or a bust) in a form common to every venue. Each post-trade client turns its
// venue's reports (FIX TradeCaptureReport, drop-copy ExecutionReport, a
// proprietary drop line) into this message and sends it to its subscriber
// (position keeping, an STP publisher, a recorder). Section 7 of each venue's
// feed.md states how its fields are filled.
//
// A client sends a TradeCapture only for an event that changed its
// post_trade::TradeLedger: duplicates and replays are dropped before they reach
// the subscriber.

#include <cmath>
#include <cstdint>
#include <string>

#include "actors/Message.hpp"
#include "enum/exch_code.hpp"

namespace post_trade
{
  enum class Event : char
  {
    New = 'N',    // a trade
    Amend = 'A',  // replaces the trade's economics; ref_trade_id names it
    Cancel = 'C', // the trade is withdrawn (by the parties or the venue)
    Bust = 'B',   // the trade is broken by the venue
  };

  enum class Side : char
  {
    Unknown = '?',
    Buy = 'B',
    Sell = 'S',
    SellShort = 'T',
    SellShortExempt = 'E',
  };

  inline const char *to_string(Event e)
  {
    switch (e)
    {
    case Event::New: return "New";
    case Event::Amend: return "Amend";
    case Event::Cancel: return "Cancel";
    case Event::Bust: return "Bust";
    }
    return "?";
  }

  inline const char *to_string(Side s)
  {
    switch (s)
    {
    case Side::Buy: return "Buy";
    case Side::Sell: return "Sell";
    case Side::SellShort: return "SellShort";
    case Side::SellShortExempt: return "SellShortExempt";
    case Side::Unknown: break;
    }
    return "Unknown";
  }

  // +1 for a buy, -1 for any sell, 0 when unknown: the sign of the position
  // change.
  inline int sign(Side s)
  {
    return s == Side::Buy ? 1 : s == Side::Unknown ? 0 : -1;
  }
}

namespace post_trade::msg
{
  struct TradeCapture : public actors::MessageT<TradeCapture>
  {
    en::x venue; // default: invalid until set

    // Identifiers. trade_id is stable across the trade's whole lifecycle and
    // unique per venue and trading day for one party's side of one trade.
    // report_id identifies this report and is the same on every delivery of it
    // (a resend, a replay), so a duplicate can be recognised. report_seq
    // orders the reports of one trading day (the venue's sequence number);
    // a report with a lower report_seq than one already applied to the same
    // trade is older.
    std::string trade_id;
    std::string report_id;
    uint64_t report_seq = 0;
    Event event = Event::New;
    std::string ref_trade_id; // Amend, Cancel, Bust: the trade they apply to
    std::string venue_order_id; // the venue's id of the order that traded, if given

    // Instrument.
    std::string symbol; // venue symbol
    int asset = -1;     // Kaspar asset id; -1 when the symbol is not mapped

    // Economics. The price is price_mantissa * 10^-price_decimals, kept as
    // the venue sent it so that nothing is lost to binary floating point.
    Side side = Side::Unknown;
    int64_t qty = 0;
    int64_t price_mantissa = 0;
    int price_decimals = 0;
    double yield = NAN; // instruments quoted in yield; NaN otherwise

    // Time. trade_date is YYYYMMDD in the venue's local calendar;
    // trade_time_ns is nanoseconds since the venue's local midnight.
    uint32_t trade_date = 0;
    int64_t trade_time_ns = 0;
    uint32_t settle_date = 0; // YYYYMMDD; 0 when the venue does not send it

    std::string account;      // the venue's account, port or give-up field
    std::string firm;         // executing or clearing firm, if given
    std::string counterparty; // contra party, if the venue discloses it

    // The report may have been delivered before (FIX PossDupFlag/PossResend,
    // or a replay after a reconnect). Informational: the ledger has already
    // removed true duplicates.
    bool poss_dup = false;

    std::string raw; // the venue's report as received

    double price() const
    {
      return double(price_mantissa) * std::pow(10.0, -price_decimals);
    }
  };
}
