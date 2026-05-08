#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"

#include <iostream>
#include <boost/format.hpp>

#include "frame/ob/OrderQNode.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/mda/OrderID.hpp"
#include "logger/act/Logger.hpp"

#include "actors/Actor.hpp"

#include "chutil/Time.hpp"

namespace frame
{
  namespace ob
  {
    class Order : public OrderQNode
    {
    private:
      int sz;
      actors::Actor *originator;
      unsigned long long id;
      uint64_t exordid;
      uint sym;
      en::bs side;
      bool deleted;
      bool cancelled;
      bool filled;
      int owner;
      uint64_t tim;

      // Order(const Order &o)  {
      //   sz=o.sz;
      //   originator=o.originator;
      //   id=o.id;
      //   exordid = o.exordid;
      //   sym=o.sym;
      //   deleted=o.deleted;
      //   cancelled=o.cancelled;
      //   filled=o.filled;
      //   owner=o.owner;
      //   tim=o.tim;
      //   aggr_fills=o.aggr_fills;
      // }

    public:
      uint64_t get_tim() const { return tim; }
      unsigned long long get_id() const { return id; }
      uint get_sym() const { return sym; }
      en::bs get_side() const { return side; }
      int get_sz() const { return sz; }
      uint64_t get_exordid() const { return exordid; }
      int aggr_fills = 0; // #nottts

      const char* get_name() const { return "Order"; }

      Order(
          uint64_t _tim,
          unsigned long long _id,
          uint64_t _exordid,
          uint _sym,
          en::bs _side,
          int _sz,
          actors::Actor *_originator,
          int owner)
          : tim(_tim), id(_id), sym(_sym), side(_side), sz(_sz), originator(_originator), deleted(false),
            cancelled(false), filled(false), owner(owner), exordid(_exordid)
      {
        aggr_fills = 0; // #nottts
        ASSERT(sz > 0, "negative order size");
        ASSERT(sz < CHOPIN_MAX_ORD_SZ, "size too large");
      }

      virtual ~Order()
      {
      }

      // check id for SIM exchange
      bool issim() const
      {
        return mda::OrderID::exchid(id) == en::x::SIM;
      }

    private:
      // notify orig
      int fill_notify(int _sz, int _disp_sz, uint px, en::mt modtyp,
                      uint64_t tim)
      {
        ASSERT(modtyp == en::mt::EXEC || modtyp == en::mt::EXECD, "invalid mod typ");
        log_dbg("sim fill_notify sz: %d, ordsz: %d, px: %d, filled: %d, cancelled: %d", _sz, sz, px, filled, cancelled);
        if (cancelled)
        {
          log_err("fill notify for an order that has been cancelled");
          ERR("filling a cancelled order");
        }
        ASSERT(issim(), "fill for real order");
        int fillsz = std::min(_sz, sz);

        if (sz >= _sz)
          sz -= _sz;
        else
          sz = 0;

        ASSERT(_disp_sz <= 0, "check this");
        ASSERT(originator, "sim orders must hve orig");
        if (originator)
        {
          ASSERT(_sz > 0, "filling for 0");
          ASSERT(issim(), "only sim orders have originator");
          auto venue = mda::OrderID::exchid(id);
          ASSERT(fillsz == _sz, "should not change for cme");
          log_dbg("sending fill id: %d, fillsz: %d, sz: %d", mda::OrderID::id(id), fillsz, sz);

#ifdef TRACEORDERS
          std::cout << ">>> FILL: " << mda::OrderID::id(id) << " " << fillsz << " " << sz << " " << tim.date << " " << tim.ns << std::endl;
#endif

          auto f = new som::msg::Fill(mda::OrderID::id(id),
                                      venue,
                                      sym,
                                      ref::Price(int(px), sym),
                                      side,
                                      fillsz,
                                      sz,
                                      owner,
                                      tim);
          originator->send(f, nullptr);
        }
        else
        {
          if (issim())
          {
            log_err("no originator -- not sending fill notify");
          }
          SNGH;
        }
        if (sz <= 0)
        {
          if (issim())
            log_dbg("size less then zero setting filled flag");
          filled = true;
        }
        return _sz;
      }

      int canc_notify(
          en::mt modtyp,
          int dispsz,
          int _sz)
      {
        ASSERT(modtyp == en::mt::CANC || modtyp == en::mt::CANCD,
               "invalid mod type for canc");
        if (issim())
          log_dbg("canc_notify sz: %d, filled: %d, cancelled: %d", _sz, filled, cancelled);
        ASSERT(sz > 0, "cancelling 0 size");

        if (filled)
        {
          log_err("cancelling an order that has already filled");
          ERR("cancelling a filled order");
        }
        if (_sz > sz)
        {
          log_err("cancelled more than was available to cancel oid: %d, exid: %d, dispsz: %d, _sz: %d : sz: %d",
                  id,
                  exordid,
                  dispsz,
                  _sz,
                  sz);
        }
        int cancsz;

        if (modtyp == en::mt::CANCD)
        {
          cancsz = sz;
          sz = 0;
        }
        else if (dispsz > 0)
        {
          cancsz = sz - dispsz;
          sz = dispsz;
        }
        else
        {
          ERR("no disp size in cancel message");
          cancsz = std::min(_sz, sz);
          sz -= cancsz;
        }
        if (originator)
        {
          ASSERT(issim(), "only sims can have originator");
          log_dbg("sending cancack id: %d, cancsz: %d, sz: %d, filled: %d, cancelled: %d",
                  mda::OrderID::id(id), cancsz, sz, filled, cancelled);
          auto f = new som::msg::CancAck(mda::OrderID::id(id), cancsz, sz);
          originator->send(f, nullptr);
        }
        else
        {
          if (issim())
            log_dbg("no orignator cannot send canc ack");
        }

        if ((sz <= 0 && dispsz <= 0) || modtyp == en::mt::CANCD)
        {
          if (issim())
            log_dbg("remaining size < 0 setting cancelled flag");
          cancelled = true;
        }

        ASSERT(sz >= 0, "size went negative");
        ASSERT(dispsz >= 0, "bad dispsz");
        return cancsz;
      }

    public:
      friend std::ostream &operator<<(std::ostream &out, const Order &o)
      {
        boost::format fmt("sym: %d, id: %d, sz: %d, side: %c");
        fmt % o.sym % o.id % o.sz % o.side;
        return out << fmt;
      }

      friend class OrderQ;
    };
  }
}
