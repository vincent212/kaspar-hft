#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "enum/exch_code.hpp"

namespace frame
{
    namespace mda
    {
        class OrderID : public actors::Message
        {
            OrderID(){}

        public:

            typedef struct
            {
                uint int0;
                uint int1;
            } two_int_t;

            typedef union
            {
                unsigned long long int64;
                two_int_t two_int;
            } compound_id_t;

            static inline unsigned long long longid(en::x exchid, uint oid)
            {
                compound_id_t ret;
                ret.two_int.int1=exchid;
                ret.two_int.int0=oid;
                return ret.int64;
            }

            static inline en::x exchid(unsigned long long longid)
            {
                compound_id_t ret;
                ret.int64=longid;
                return static_cast<en::x>(ret.two_int.int1);
            }

            static inline uint id(unsigned long long longid)
            {
                compound_id_t ret;
                ret.int64=longid;
                return ret.two_int.int0;
            }

            static inline bool is_sim(unsigned long long id)
            {
                return exchid(id)==en::x::SIM;
            }

        };

    }
}





        
