#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace frame
{
  namespace som
  {
    namespace msg
    {
      struct Reject : public  actors::Message_N<40> , public actors::MemoryPool<Reject>
      {
        enum
        {
          UNKNOWN, OVERLIMIT, THROTTLE, EXCHLIMIT, DISCONNECTED, NOTLOGGEDIN
        };
        uint id = 0;
        uint64_t xordid = 0;
        int reason = UNKNOWN;
        Reject(uint _id, int _reason = UNKNOWN)
            : id(_id),
              reason(_reason)
        {
        }
        Reject(uint _id, uint64_t _xordid, int _reason)
            : id(_id),
              reason(_reason),
              xordid(_xordid)
        {
        }
        Reject() = default;

        // Serialize to boost property tree (JSON)
        void serialize(boost::property_tree::ptree &pt) const {
          pt.put("id", id);
          pt.put("xordid", xordid);
          pt.put("reason", reason);
        }

        // Deserialize from boost property tree (JSON)
        static Reject deserialize(const boost::property_tree::ptree &pt) {
          Reject r;
          r.id = pt.get<uint>("id");
          r.xordid = pt.get<uint64_t>("xordid", 0);
          r.reason = pt.get<int>("reason", UNKNOWN);
          return r;
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
        static Reject from_json(const std::string &json) {
          boost::property_tree::ptree pt;
          std::istringstream is(json);
          boost::property_tree::read_json(is, pt);
          return deserialize(pt);
        }
      };
    }
  }
}
