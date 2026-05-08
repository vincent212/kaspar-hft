#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace frame
{
    namespace som
    {
        namespace msg
        {
            struct Ack : public  actors::Message_N<31> , public actors::MemoryPool<Ack>
            {
                uint id = 0;
                uint64_t xordid = 0;
                Ack(uint _id) : id(_id)
                {
                }
                Ack(uint _id, uint64_t _xordid) : id(_id), xordid(_xordid)
                {
                }
                Ack() = default;

                // Serialize to boost property tree (JSON)
                void serialize(boost::property_tree::ptree &pt) const {
                    pt.put("id", id);
                    pt.put("xordid", xordid);
                }

                // Deserialize from boost property tree (JSON)
                static Ack deserialize(const boost::property_tree::ptree &pt) {
                    Ack a;
                    a.id = pt.get<uint>("id");
                    a.xordid = pt.get<uint64_t>("xordid", 0);
                    return a;
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
                static Ack from_json(const std::string &json) {
                    boost::property_tree::ptree pt;
                    std::istringstream buf(json);
                    boost::property_tree::read_json(buf, pt);
                    return deserialize(pt);
                }
            };
        }
    }
}
