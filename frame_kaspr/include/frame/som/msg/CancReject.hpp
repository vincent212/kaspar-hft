#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace frame::som::msg
{

    struct CancReject : public  actors::Message_N<33> , public actors::MemoryPool<CancReject>
    {
        enum
        {
            NOTFOUND,
            FILLEDALREADY,
            CANCELLEDALREADY,
            REJECTEDPREVIOUSLY,
            NOTACKED,
            INTERNALOIDTOCANC,
            REJECTONMOD,
            FROMEXCHANGE,
            FROMEXCHANGE_BUSINESSREJECT,
            THROTTLE,
            DISCONNECTED,
            NOTLOGGEDIN,
            UNKNOWN
        };
        uint id = 0;
        int ord_status = 0;
        uint64_t xordid = 0;
        int reason = UNKNOWN;

        CancReject(uint _id, int _reason, int _ord_status = 99)
            : id(_id), reason(_reason), ord_status(_ord_status)
        {
            ASSERT(reason != UNKNOWN, "Must specify canc reject reason");
        }
        CancReject(uint _id, uint64_t _xordid, int _reason)
            : id(_id), xordid(_xordid), reason(_reason)
        {
            ASSERT(reason != UNKNOWN, "Must specify canc reject reason");
        }
        CancReject() = default;

        // Serialize to boost property tree (JSON)
        void serialize(boost::property_tree::ptree &pt) const {
            // pt.put("id", id);
            // pt.put("ord_status", ord_status);
            pt.put("xordid", xordid);
            pt.put("reason", reason);
        }

        // Deserialize from boost property tree (JSON)
        static CancReject deserialize(const boost::property_tree::ptree &pt) {
            CancReject c;
            // c.id = pt.get<uint>("id");
            // c.ord_status = pt.get<int>("ord_status", 0);
            c.xordid = pt.get<uint64_t>("xordid", 0);
            c.reason = pt.get<int>("reason", UNKNOWN);
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
        static CancReject from_json(const std::string &json) {
            boost::property_tree::ptree pt;
            std::istringstream buf(json);
            boost::property_tree::read_json(buf, pt);
            return deserialize(pt);
        }
    };
}
