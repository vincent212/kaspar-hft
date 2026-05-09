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
            struct CancAck : public actors::Message_N<32> , public actors::MemoryPool<CancAck>
            {
                uint id = 0;
                int fillsz = 0; // how much got cancelled
                int sz = 0; // how much remains

                CancAck(uint _id):id(_id),fillsz(0),sz(0)
                {}

                CancAck(uint _id,int _fillsz,int _sz)
                    :id(_id),fillsz(_fillsz),sz(_sz)
                {}

                CancAck() = default;

                // Serialize to boost property tree (JSON)
                void serialize(boost::property_tree::ptree &pt) const {
                    pt.put("id", id);
                    // pt.put("fillsz", fillsz);
                    // pt.put("sz", sz);
                }

                // Deserialize from boost property tree (JSON)
                static CancAck deserialize(const boost::property_tree::ptree &pt) {
                    CancAck c;
                    c.id = pt.get<uint>("id");
                    // c.fillsz = pt.get<int>("fillsz");
                    // c.sz = pt.get<int>("sz");
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
                static CancAck from_json(const std::string &json) {
                    boost::property_tree::ptree pt;
                    std::istringstream buf(json);
                    boost::property_tree::read_json(buf, pt);
                    return deserialize(pt);
                }
            };
        }
    }
}
