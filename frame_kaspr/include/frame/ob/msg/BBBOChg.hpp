#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "enum/e_names.hpp"
#include "chutil/Time.hpp"
#include "frame/ref/Price.hpp"
#include <iostream>

namespace frame
{
  namespace ob
  {
    namespace msg
    {
      struct BBBOChg : public actors::Message_N<22>  , public actors::MemoryPool<BBBOChg, 16, 16, 1024>
      {
        en::x venue;
        int best_bid;
        int best_ask;
        uint sym;
        en::bs side; // to do: remove this?
        uint64_t tx_time;
                
        BBBOChg(
                uint64_t _tx_time,
                en::x _venue,en::bs _side,
                uint _sym,
                int bb,
                int ba)
          :tx_time(_tx_time),
           venue(_venue),side(_side),sym(_sym),best_bid(bb),best_ask(ba)
        {
          ASSERT(tx_time > 0, "invalid txtime");
        }

        BBBOChg() {};

        inline friend std::ostream& operator<<(std::ostream& out, const BBBOChg& m)
        {
          return out
            << "venue: " << en::to_string(m.venue)
            << " sym: " << m.sym
            << " side: " << en::to_string(m.side)
            << " best_bid: " << m.best_bid
            << " best_ask: " << m.best_ask;
        }

        inline bool operator==(const BBBOChg& other) const
        {
          return venue == other.venue &&
                 (best_bid <= 0 || best_bid == other.best_bid) &&
                 (best_ask >= 99000 || best_ask == other.best_ask) &&
                 sym == other.sym &&
                 //side == other.side && <-- IGNORE --> should not be used
                 tx_time == other.tx_time;
        }

		inline std::string to_string() const
		{
			std::ostringstream o;
			o << *this;
			return o.str();
		}
      };
    }
  }
}



