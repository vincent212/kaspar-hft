#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <limits>
#include <string>
#include <sstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "chutil/Macros.hpp"
#include "actors/Message.hpp"
#include "enum/e_names.hpp"
#include "chutil/Time.hpp"

#include "frame/som/msg/Ack.hpp"
#include "actors/Actor.hpp"

namespace frame
{
  namespace som
  {
    namespace msg
    {
      struct Order : public  actors::Message_N<38>
      {
        uint64_t ts = 0;
        en::x venue;
        int sym = std::numeric_limits<int>::max();
        double sz = 0;
        int px = 0;
        int still_to_be_filled = 0;
        int oid = -1; // assigned by som
        en::bs side;
        en::trader owner = 0; // book  mapping
        unsigned long long order_ref = 0; // this is to be assigned by the SOM only
        mutable int oid_to_cancel = -1;
        uint8_t fenics_visibility_groups = 0;
        int64_t lock_price = 0; // for fenics only must be 0
        std::string dealerweb_trading_account;
        std::string fenics_originating_trader;
        std::string prefix;
        std::string cusip;

        // hack for SOM status
        bool filled = false, canced = false, rejed = false, acked = false, unacked_slated_for_canc = false;

        Order()
        {
        }
        Order(
          const std::string &prefix,
          en::x _venue, uint _sym, double _sz, int _px, 
          en::bs _side, int _owner,
          uint8_t fenics_visibility_groups = 0
        )
          : prefix(prefix),
          venue(_venue), sym(_sym), sz(_sz), 
          px(_px), side(_side), owner(_owner),
          fenics_visibility_groups(fenics_visibility_groups)
        {
          still_to_be_filled = sz;
          ASSERT(sz > 0, "negative order size");
          if (fenics_visibility_groups)
          {
            ASSERT(venue == en::x::FENICS || venue == en::x::FENICSOTR || venue == en::x::FENICSPREMIUM ||
                   venue == en::x::FENICSDVRV,
              "fenics visibility groups only for fenics");
          }
        }

        static int
          send(
            const std::string& prefix,
            uint64_t ts,
            actors::Actor* sender,
            actor_ptr som,
            en::x venue, 
            uint sym, 
            double sz, 
            int px, 
            en::bs side, 
            int owner,
            uint8_t fenics_visibility_groups = 0
          )
        {
          Order o(
            prefix,
            venue,
            sym,
            sz,
            px,
            side,
            owner,
            fenics_visibility_groups
            );
          o.ts=ts;
          auto m = som->fast_send(&o, sender);
          auto ack = static_cast<const frame::som::msg::Ack*>(m.get());
          auto id = ack->id;
          return id;
        }

        // Serialize to boost property tree (JSON)
        void serialize(boost::property_tree::ptree &pt) const {
          pt.put("venue", static_cast<int>(venue));
          pt.put("sym", sym);
          pt.put("sz", sz);
          pt.put("px", px);
          //pt.put("still_to_be_filled", still_to_be_filled);
          pt.put("oid", oid);
          pt.put("side", static_cast<int>(side));
          // pt.put("owner", static_cast<int>(owner));
          // pt.put("order_ref", order_ref);
          // pt.put("oid_to_cancel", oid_to_cancel);
          // pt.put("fenics_visibility_groups", fenics_visibility_groups);
          // pt.put("lock_price", lock_price);
          // pt.put("dealerweb_trading_account", dealerweb_trading_account);
          // pt.put("prefix", prefix);
          pt.put("cusip", cusip); // <-- add cusip
          // pt.put("filled", filled);
          // pt.put("canced", canced);
          // pt.put("rejed", rejed);
          // pt.put("acked", acked);
          // pt.put("unacked_slated_for_canc", unacked_slated_for_canc);
        }

        // Deserialize from boost property tree (JSON)
        static Order deserialize(const boost::property_tree::ptree &pt) {
          Order o;
          //o.ts = pt.get<uint64_t>("ts");
          o.venue = static_cast<en::x>(pt.get<int>("venue"));
          o.sym = pt.get<int>("sym");
          o.sz = pt.get<double>("sz");
          o.px = pt.get<int>("px");
          //o.still_to_be_filled = pt.get<int>("still_to_be_filled");
          o.oid = pt.get<int>("oid");
          o.side = static_cast<en::bs>(pt.get<int>("side"));
          // o.owner = static_cast<en::trader>(pt.get<int>("owner"));
          // o.order_ref = pt.get<unsigned long long>("order_ref");
          // o.oid_to_cancel = pt.get<int>("oid_to_cancel", -1);
          // o.fenics_visibility_groups = pt.get<uint8_t>("fenics_visibility_groups");
          // o.lock_price = pt.get<int64_t>("lock_price");
          // o.dealerweb_trading_account = pt.get<std::string>("dealerweb_trading_account", "");
          // o.prefix = pt.get<std::string>("prefix", "");
          o.cusip = pt.get<std::string>("cusip", ""); // <-- add cusip
          // o.filled = pt.get<bool>("filled", false);
          // o.canced = pt.get<bool>("canced", false);
          // o.rejed = pt.get<bool>("rejed", false);
          // o.acked = pt.get<bool>("acked", false);
          // o.unacked_slated_for_canc = pt.get<bool>("unacked_slated_for_canc", false);
          return o;
        }

        // Optionally, serialize to JSON string
        std::string to_json() const {
          boost::property_tree::ptree pt;
          serialize(pt);
          std::ostringstream buf;
          boost::property_tree::write_json(buf, pt, false);
          return buf.str();
        }

        // Optionally, create from JSON string
        static Order from_json(const std::string &json) {
          boost::property_tree::ptree pt;
          std::istringstream buf(json);
          boost::property_tree::read_json(buf, pt);
          return deserialize(pt);
        }
      };
    }
  }
}
