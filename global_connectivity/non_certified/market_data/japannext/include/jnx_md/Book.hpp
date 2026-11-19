#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The handler's own order book: every resting order, per instrument, per side,
// per price, in queue order. The dispatcher keeps it so that execute, delete and
// replace find their order (they carry no book), so that the replay can compare
// OB with it, and so that a GLIMPSE snapshot can be written from it.
//
// Queue order follows OB's rule (kaspar_mapping.md): a replace at the same price
// and the same or a smaller size keeps the order's place; any other replace goes
// to the back of its new level. A partial execution keeps the place.

#include <cstdint>
#include <functional>
#include <list>
#include <map>

namespace jnx_md
{
  struct Instrument;

  struct Order
  {
    uint64_t number = 0;     // the venue's current Order Number
    uint64_t oid = 0;        // orderID in the records; kept across replaces
    uint64_t priority = 0;   // sequence number of the message that added it
    Instrument *inst = nullptr;
    uint32_t px = 0;         // venue price units (0.1 JPY)
    uint32_t qty = 0;
    char side = 'B';         // 'B' or 'S', as the feed has it
    char order_type = ' ';   // F's Order Type ('Q' DLP); ' ' for A
    std::list<Order *>::iterator pos;
  };

  struct Level
  {
    std::list<Order *> q;
    uint64_t qty = 0;
  };

  struct Book
  {
    std::map<uint32_t, Level, std::greater<uint32_t>> bids;   // best first
    std::map<uint32_t, Level> asks;                           // best first

    void insert(Order *o)
    {
      Level &l = o->side == 'B' ? bids[o->px] : asks[o->px];
      l.q.push_back(o);
      o->pos = std::prev(l.q.end());
      l.qty += o->qty;
    }

    void remove(Order *o)
    {
      if (o->side == 'B')
        remove_from(bids, o);
      else
        remove_from(asks, o);
    }

    // Size change in place (execution, or a replace that keeps the place).
    void resize(Order *o, uint32_t qty)
    {
      Level &l = o->side == 'B' ? bids[o->px] : asks[o->px];
      l.qty = l.qty - o->qty + qty;
      o->qty = qty;
    }

    uint32_t best_bid() const { return bids.empty() ? 0 : bids.begin()->first; }
    uint32_t best_ask() const { return asks.empty() ? 0 : asks.begin()->first; }
    bool empty() const { return bids.empty() && asks.empty(); }
    void clear()
    {
      bids.clear();
      asks.clear();
    }

  private:
    template <class M>
    static void remove_from(M &m, Order *o)
    {
      auto it = m.find(o->px);
      if (it == m.end())
        return;
      it->second.qty -= o->qty;
      it->second.q.erase(o->pos);
      if (it->second.q.empty())
        m.erase(it);
    }
  };
}
