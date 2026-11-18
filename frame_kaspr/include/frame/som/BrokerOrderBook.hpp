/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// BrokerOrderBook: the orders one broker order manager has in flight.
//
// A plain class, not an actor. Each broker order manager owns one and calls it
// only from its own actor thread, so it has no locking.
//
// It does the bookkeeping every broker needs and nothing broker-specific:
//   - assigns client ids that stay unique for the whole trading day, across
//     restarts (FIX ClOrdIDs need this, and so do broker APIs that key orders
//     by a client-assigned id)
//   - maps every client id an order has used (new, cancel, replace) back to
//     the SOM's order id, so a late report on an old id still resolves
//   - tracks state, quantity, filled and leaves, and checks each event
//     against them
//   - reports anything it does not recognise as an Outcome instead of
//     asserting: the order manager logs it and carries on
//
// The order manager translates Results into SOM messages (Ack, Fill, CancAck,
// CancReject, Reject) and broker requests.

#include <cstdint>
#include <string>
#include <unordered_map>

#include "frame/som/msg/Order.hpp"

namespace actors
{
  class Actor;
}

namespace frame::som
{
  using ClientId = uint64_t;

  // Client ids unique for a trading day: (milliseconds since midnight at
  // creation) << 36 | counter; 2^36 orders per generator. A restart starts
  // from a later millisecond, so it never reuses an id, and from_clock()
  // never gives two generators in one process the same millisecond. Two
  // processes sharing one broker session must not start in the same
  // millisecond. The result fits in 63 bits, so it is also a valid positive
  // decimal ClOrdID.
  class ClientIdGen
  {
  public:
    static constexpr int COUNTER_BITS = 36;
    static constexpr uint64_t MS_PER_DAY = 86'400'000ULL;

    explicit ClientIdGen(uint64_t ms_since_midnight)
        : base_((ms_since_midnight % MS_PER_DAY) << COUNTER_BITS) {}

    // Milliseconds since midnight UTC, now; later than any earlier
    // from_clock() in this process.
    static ClientIdGen from_clock();

    ClientId next() { return base_ | (++n_ & ((1ULL << COUNTER_BITS) - 1)); }

  private:
    uint64_t base_;
    uint64_t n_ = 0;
  };

  enum class State
  {
    PendingNew,     // sent, not yet accepted
    Working,        // accepted, live at the broker
    PendingCancel,  // cancel sent
    PendingReplace, // cancel/replace sent
    Filled,         // done: fully filled
    Canceled,       // done: cancelled (by us or by the broker/exchange)
    Rejected,       // done: rejected
  };

  inline bool is_done(State s)
  {
    return s == State::Filled || s == State::Canceled || s == State::Rejected;
  }
  const char *to_string(State s);

  struct Entry
  {
    int oid = -1;                    // SOM order id
    actors::Actor *sender = nullptr; // who gets the replies (the SOM)
    frame::som::msg::Order order;    // as received
    ClientId cid = 0;                // id the order is live under at the broker
    ClientId pending_cid = 0;        // id of the cancel or replace in flight, 0 = none
    State state = State::PendingNew;
    bool accepted = false;           // the broker acknowledged (or filled) the order
    int qty = 0;    // total order quantity (changes on replace)
    int cum = 0;    // filled so far
    int leaves = 0; // qty - cum while live, 0 once done
    int px = 0;     // limit price in ticks (changes on replace)
    int pending_qty = 0, pending_px = 0; // replace target
    uint64_t exch_oid = 0;               // exchange order id, from the ack
  };

  enum class Outcome
  {
    Ok,        // applied
    Unknown,   // no order has this id: log; a fill on it needs attention
    Duplicate, // add() with an oid already tracked
    Late,      // the order is already done; event ignored
    Busy,      // a cancel or replace is already in flight
    Invalid,   // event does not fit the order (overfill, bad quantity, a
               // reject on an accepted order); see each call for its effect
  };
  const char *to_string(Outcome o);

  struct Result
  {
    Outcome outcome = Outcome::Unknown;
    Entry *entry = nullptr; // set unless Unknown
    int fill_qty = 0;       // on_fill: quantity of this fill
    bool done = false;      // the event finished the order
  };

  class BrokerOrderBook
  {
  public:
    explicit BrokerOrderBook(ClientIdGen gen) : gen_(gen) {}

    // --- requests we send -------------------------------------------------
    // New order: assigns the order's client id (entry->cid). Quantities are
    // whole units (shares, contracts, lots): a size that is not a positive
    // integer is Invalid; a venue with fractional sizes scales them first.
    Result add(const frame::som::msg::Order &o, actors::Actor *sender);

    // Cancel: assigns entry->pending_cid (FIX needs a new ClOrdID for the
    // cancel; an API that cancels by the order's own id can ignore it).
    Result request_cancel(int oid);

    // Cancel/replace to a new total quantity and price; assigns
    // entry->pending_cid, the id the order lives under once replaced.
    Result request_replace(int oid, int new_qty, int new_px);

    // The broker API refused a request synchronously (a busy connection,
    // FIX session down). A refused new order is rejected; a refused cancel or
    // replace leaves the order as it was.
    Result send_failed(ClientId cid);

    // --- reports we receive (cid: any id the order has used) --------------
    Result on_accepted(ClientId cid, uint64_t exch_oid = 0);
    Result on_fill(ClientId cid, int qty);
    Result on_canceled(ClientId cid);        // ours or unsolicited
    Result on_cancel_rejected(ClientId cid); // cancel refused, order still live
    Result on_replaced(ClientId cid);
    Result on_replace_rejected(ClientId cid);
    Result on_rejected(ClientId cid); // new order rejected

    // --- lookups ----------------------------------------------------------
    const Entry *find_oid(int oid) const;
    const Entry *find_cid(ClientId cid) const;
    size_t size() const { return by_oid_.size(); }
    size_t live() const;

    // Forget finished orders (and all their ids). Late reports for them then
    // come back Unknown, so call it at a quiet point, e.g. end of day.
    void purge_done();

  private:
    Entry *lookup(ClientId cid);
    Result finish(Entry &e, State s);

    ClientIdGen gen_;
    std::unordered_map<int, Entry> by_oid_;      // node-based: Entry* stays valid
    std::unordered_map<ClientId, int> cid_oid_;  // every id ever used -> oid
  };
}
