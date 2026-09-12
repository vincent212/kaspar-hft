#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "frame/ob/msg/EndOfBurst2.hpp"
#include "frame/ob/msg/EndOfBurst.hpp"
#include "frame/mda/msg/Data.hpp"
#include "chutil/Time.hpp"
#include "enum/e_names.hpp"
#include <cstdint>
#include <chrono>

namespace unit_test {

/**
 * FakeMarketData - Helpers for creating fake market data messages in tests
 *
 * The trading system uses two types of EndOfBurst messages:
 *
 * 1. EndOfBurst2 - Simple struct with direct fields (txtim, bb, ba, etc.)
 *    Used by: Timer (reads txtim to advance time)
 *
 * 2. EndOfBurst - Contains a payload pointer to data_pay_load
 *    Used by: Aggregator, light22 (for full market data)
 *
 * IMPORTANT: Time in this system is NOT wall-clock time!
 * All time comes from market data messages. Timer advances its internal
 * clock based on EndOfBurst2.txtim.
 */

//=============================================================================
// Timestamp Helpers
//=============================================================================

/**
 * Create a UTC timestamp from date/time components
 * @return Epoch nanoseconds suitable for txtim fields
 *
 * IMPORTANT: All times are interpreted as UTC to match exchange timestamps.
 * Timer uses Time::from_epoch() which calls gmtime_r(), so hour() returns UTC hour.
 */
inline uint64_t make_timestamp(int year, int month, int day,
                                int hour, int min, int sec = 0) {
  // Use C++20 chrono for UTC time calculation
  auto ymd = std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)}
             / std::chrono::day{static_cast<unsigned>(day)};
  auto date = std::chrono::sys_days{ymd};
  auto time_of_day = std::chrono::hours{hour} + std::chrono::minutes{min} + std::chrono::seconds{sec};
  auto utc_time = date + time_of_day;

  // Convert to nanoseconds since epoch
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      utc_time.time_since_epoch()).count();
}

/**
 * Create a timestamp for "today" at a given time
 * Uses 2024-01-15 as the default test date
 */
inline uint64_t make_timestamp_today(int hour, int min, int sec = 0) {
  return make_timestamp(2024, 1, 15, hour, min, sec);
}

//=============================================================================
// EndOfBurst2 - For Timer tests
//=============================================================================

/**
 * Create an EndOfBurst2 message at a specific time
 *
 * Timer uses EndOfBurst2 to advance its internal clock.
 * The txtim field contains the exchange timestamp in nanoseconds.
 *
 * @param year  Year (e.g., 2024)
 * @param month Month (1-12)
 * @param day   Day (1-31)
 * @param hour  Hour (0-23)
 * @param min   Minute (0-59)
 * @param sec   Second (0-59)
 * @param sym   Symbol ID (default 1)
 * @return Heap-allocated EndOfBurst2 (caller takes ownership)
 *
 * Example:
 *   auto eob2 = make_eob2_at(2024, 1, 15, 9, 30, 0);  // 9:30 AM
 *   timer->process_message(eob2, &mock_ob);
 */
inline frame::ob::msg::EndOfBurst2* make_eob2_at(
    int year, int month, int day,
    int hour, int min, int sec = 0,
    int sym = 1)
{
  auto eob2 = new frame::ob::msg::EndOfBurst2();
  eob2->txtim = make_timestamp(year, month, day, hour, min, sec);
  eob2->sym = sym;
  eob2->bb = 100;
  eob2->ba = 101;
  eob2->bs = 1000;
  eob2->as = 1000;
  eob2->mid = 100.5;
  eob2->mev = en::md::ADD;
  eob2->action = en::mt::NONE;
  eob2->side = en::bs::BUY;
  eob2->volumefound = 0;
  eob2->num_trad = 0;
  return eob2;
}

/**
 * Create an EndOfBurst2 message using default test date (2024-01-15)
 */
inline frame::ob::msg::EndOfBurst2* make_eob2_today(
    int hour, int min, int sec = 0, int sym = 1)
{
  return make_eob2_at(2024, 1, 15, hour, min, sec, sym);
}

//=============================================================================
// EndOfBurst with Payload - For Aggregator and light22 tests
//=============================================================================

/**
 * Create a market data payload
 *
 * The payload contains all market data for an update, including:
 * - Market event (ADD, MOD, DEL, etc.)
 * - Price and size of the update
 * - BBBO (best bid/best offer) in the point_ structure
 *
 * @param venue    Market venue (e.g., en::x::CMEMD)
 * @param sym      Symbol ID
 * @param price    Price of this update (integer ticks)
 * @param size     Size of this update
 * @param event    Market event (ADD, MOD, DEL, etc.)
 * @param best_bid Best bid price (for point_.bid_px[0])
 * @param best_ask Best ask price (for point_.ask_px[0])
 * @return boost::intrusive_ptr to payload (reference counted)
 *
 * Example:
 *   auto payload = make_payload(en::x::CMEMD, 1, 100, 50, en::md::ADD);
 *   auto eob = make_eob(payload, true);
 *   light22->process_message(eob, &mock_aggregator);
 */
inline boost::intrusive_ptr<frame::mda::msg::data_pay_load> make_payload(
    en::x venue,
    int sym,
    [[maybe_unused]] int price,
    int size,
    en::md event,
    int best_bid = 100,
    int best_ask = 101)
{
  auto payload = boost::intrusive_ptr<frame::mda::msg::data_pay_load>(
      new frame::mda::msg::data_pay_load());

  // Basic fields
  payload->mkt = venue;
  payload->sym = sym;
  // Don't set payload->px - requires RefData::get_asset which needs registration
  // Tests can set it manually if needed: payload->px = Price(price, asset_ptr)
  payload->sz = size;
  payload->disp_sz = size;
  payload->mev = event;
  payload->action = en::mt::NONE;
  payload->side = en::bs::BUY;
  payload->recovery = false;
  payload->eoe = false;
  payload->ex_order_id = 0;

  // Timestamps
  payload->txtim_epoch = make_timestamp_today(9, 0, 0);
  payload->sendtim_epoch = payload->txtim_epoch;
  payload->hndl_tim_epoch = payload->txtim_epoch;
  payload->tim = payload->txtim_epoch;
  payload->send_tim = payload->txtim_epoch;
  payload->ts0 = payload->txtim_epoch;

  // Point structure (BBBO)
  payload->point_.baddata = false;
  payload->point_.sym = sym;
  payload->point_.side = en::bs::BUY;
  payload->point_.action = en::mt::NONE;
  payload->point_.mev = event;

  // Set best bid/ask at level 0
  payload->point_.bid_px[0] = best_bid;
  payload->point_.ask_px[0] = best_ask;
  payload->point_.bid_sz[0] = 1000;
  payload->point_.ask_sz[0] = 1000;

  // Zero out other levels
  for (int i = 1; i < NLEVELS; ++i) {
    payload->point_.bid_px[i] = 0;
    payload->point_.ask_px[i] = 0;
    payload->point_.bid_sz[i] = 0;
    payload->point_.ask_sz[i] = 0;
  }

  return payload;
}

/**
 * Create a payload with timestamp
 */
inline boost::intrusive_ptr<frame::mda::msg::data_pay_load> make_payload_at(
    int year, int month, int day, int hour, int min, int sec,
    en::x venue,
    int sym,
    int price,
    int size,
    en::md event,
    int best_bid = 100,
    int best_ask = 101)
{
  auto payload = make_payload(venue, sym, price, size, event, best_bid, best_ask);
  uint64_t ts = make_timestamp(year, month, day, hour, min, sec);
  payload->txtim_epoch = ts;
  payload->sendtim_epoch = ts;
  payload->hndl_tim_epoch = ts;
  payload->tim = ts;
  payload->send_tim = ts;
  payload->ts0 = ts;
  return payload;
}

/**
 * Create an EndOfBurst message from a payload
 *
 * @param payload The market data payload
 * @param last    True if this is the last update in a batch (usually true)
 * @return Heap-allocated EndOfBurst (caller takes ownership)
 */
inline frame::ob::msg::EndOfBurst* make_eob(
    boost::intrusive_ptr<const frame::mda::msg::data_pay_load> payload,
    bool last = true)
{
  auto eob = new frame::ob::msg::EndOfBurst();
  eob->payload = payload;
  eob->last = last;
  return eob;
}

/**
 * Convenience: Create EndOfBurst with new payload in one call
 */
inline frame::ob::msg::EndOfBurst* make_eob_with_payload(
    en::x venue,
    int sym,
    int price,
    int size,
    en::md event,
    int best_bid = 100,
    int best_ask = 101,
    bool last = true)
{
  auto payload = make_payload(venue, sym, price, size, event, best_bid, best_ask);
  return make_eob(payload, last);
}

/**
 * Create an EndOfBurst with payload at a specific timestamp
 */
inline frame::ob::msg::EndOfBurst* make_eob_at(
    int year, int month, int day, int hour, int min, int sec,
    en::x venue,
    int sym,
    int price,
    int size,
    en::md event,
    int best_bid = 100,
    int best_ask = 101,
    bool last = true)
{
  auto payload = make_payload_at(year, month, day, hour, min, sec,
                                  venue, sym, price, size, event, best_bid, best_ask);
  return make_eob(payload, last);
}

//=============================================================================
// Trade Message Helpers
//=============================================================================

/**
 * Create a trade (execution) payload
 *
 * IMPORTANT.  On a real EXEC, `payload->side` is the PASSIVE (resting)
 * order's side, not the aggressor's.  The feed gives us the resting order
 * (TachBook2 looks it up by orderID); the aggressor is inferred by
 * inverting it.  Tests must pass the RESTING side here or they will not
 * match production.
 *
 * Reference definition is is_hit() / is_tak() in Data.hpp:157-165:
 *   resting BUY (a bid)    -> the bid was hit    -> is_hit()  -> aggressor SOLD
 *   resting SEL (an offer) -> the offer was taken -> is_tak() -> aggressor BOUGHT
 *
 * @param resting_side Side of the order that was already in the book.
 */
inline boost::intrusive_ptr<frame::mda::msg::data_pay_load> make_trade_payload(
    en::x venue,
    int sym,
    int price,
    int size,
    en::bs resting_side,
    int best_bid = 100,
    int best_ask = 101)
{
  auto payload = make_payload(venue, sym, price, size, en::md::MOD, best_bid, best_ask);
  payload->action = en::mt::EXEC;
  payload->side = resting_side;
  payload->point_.side = resting_side;
  payload->point_.action = en::mt::EXEC;
  payload->point_.mev = en::md::MOD;
  return payload;
}

/**
 * Create a cancel payload
 */
inline boost::intrusive_ptr<frame::mda::msg::data_pay_load> make_cancel_payload(
    en::x venue,
    int sym,
    int price,
    int size,
    en::bs side,
    int best_bid = 100,
    int best_ask = 101)
{
  auto payload = make_payload(venue, sym, price, size, en::md::MOD, best_bid, best_ask);
  payload->action = en::mt::CANCD;
  payload->side = side;
  payload->point_.side = side;
  payload->point_.action = en::mt::CANCD;
  payload->point_.mev = en::md::MOD;
  return payload;
}

} // namespace unit_test
