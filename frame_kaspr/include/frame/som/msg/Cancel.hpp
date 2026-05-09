#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include "actors/Actor.hpp"
#include <limits.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace frame
{
  namespace som
  {
    namespace msg
    {
      struct Cancel : public  actors::Message_N<34> , public actors::MemoryPool<Cancel>
      {
        uint id;
        uint32_t sz; // partial cancels not supported
        uint64_t order_ref = 0; // this is only set by the SOM
        std::string dealerweb_trading_account;

        Cancel(
          uint _id
        ) :
          id(_id)
        {
          sz = std::numeric_limits<int>::max();
          order_ref = 0;
        }

        Cancel() { id = -1; }

        // Serialize to boost property tree (JSON)
        void serialize(boost::property_tree::ptree &pt) const {
          pt.put("id", id);
          //pt.put("sz", sz);
        }

        // Deserialize from boost property tree (JSON)
        static Cancel deserialize(const boost::property_tree::ptree &pt) {
          Cancel c;
          c.id = pt.get<uint>("id");
          //c.sz = pt.get<uint32_t>("sz");
          return c;
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
        static Cancel from_json(const std::string &json) {
          boost::property_tree::ptree pt;
          std::istringstream buf(json);
          boost::property_tree::read_json(buf, pt);
          return deserialize(pt);
        }

        static void
          send(actors::Actor* sender, actor_ptr som,
            uint32_t id)
        {
          auto o = new Cancel(id);
          som->send(o, sender);
        }

      };
    }
  }
}
