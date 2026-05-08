#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include <list>
#include <string>
#include <sstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "enum/e_names.hpp"
#include "frame/ref/Price.hpp"
#include "chutil/Time.hpp"

namespace frame
{
  namespace som
  {
    namespace msg
    {
      struct Fill : public actors::Message_N<35> , public actors::MemoryPool<Fill>
      {
        uint id = 0;
        double sz = 0;
        uint sym = 0;
        double still_to_be_filled;
        int pxi = 0;
        ref::Price px;
        en::bs side;
        en::x venue;
        uint64_t xordid = 0;
        en::trader owner;
        uint64_t tim = 0;

        Fill(uint _id,
             en::x _venue,
             uint _sym,
             const ref::Price& _px,
             en::bs _side,
             double _sz,
             double _still_to_be_filled,
             en::trader _owner,
             uint64_t _tim)
          :
          id(_id),venue(_venue),sym(_sym),px(_px),side(_side),
          sz(_sz),still_to_be_filled(_still_to_be_filled),owner(_owner),tim(_tim)
        {
          pxi = px.to_int();
        }

        Fill(uint _id,
             en::x _venue,
             uint _sym,
             int _pxi,
             en::bs _side,
             double _sz,
             double _still_to_be_filled,
             en::trader _owner,
             uint64_t _tim = 0)
          :
          id(_id),venue(_venue),sym(_sym),pxi(_pxi),side(_side),
          sz(_sz),still_to_be_filled(_still_to_be_filled),owner(_owner),tim(_tim)
        {
          px = ref::Price(pxi, sym);
        }

        Fill() = default;

        std::string to_string() const
        {
          return 
            "id: " + std::to_string(id) 
            + " side: " + en::to_string(side)
            + " sym: " + std::to_string(sym) 
            + " px: " + std::to_string(px.to_int()) 
            + " sz: " + std::to_string(sz)
            + " stbf: " + std::to_string(still_to_be_filled) 
            + " owner: " + std::to_string(owner);
        }

        // Serialize to boost property tree (JSON)
        void serialize(boost::property_tree::ptree &pt) const {
          //pt.put("id", id);
          pt.put("sz", sz);
          //pt.put("sym", sym);
          pt.put("still_to_be_filled", still_to_be_filled);
          //pt.put("pxi", px.to_int());
          //pt.put("side", static_cast<int>(side));
          //pt.put("venue", static_cast<int>(venue));
          pt.put("xordid", xordid);
          //pt.put("owner", static_cast<int>(owner));
        }

        // Deserialize from boost property tree (JSON)
        static Fill deserialize(const boost::property_tree::ptree &pt) {
          Fill f;
          //f.id = pt.get<uint>("id");
          f.sz = pt.get<double>("sz");
          //f.sym = pt.get<uint>("sym");
          f.still_to_be_filled = pt.get<double>("still_to_be_filled");
          //f.pxi = pt.get<int>("pxi");
          //f.px = ref::Price(f.pxi, f.sym);
          //f.side = static_cast<en::bs>(pt.get<int>("side"));
          //f.venue = static_cast<en::x>(pt.get<int>("venue"));
          f.xordid = pt.get<uint64_t>("xordid", 0);
          //f.owner = static_cast<en::trader>(pt.get<int>("owner"));
          return f;
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
        static Fill from_json(const std::string &json) {
          boost::property_tree::ptree pt;
          std::istringstream buf(json);
          boost::property_tree::read_json(buf, pt);
          return deserialize(pt);
        }
      };
    }
  }
}
