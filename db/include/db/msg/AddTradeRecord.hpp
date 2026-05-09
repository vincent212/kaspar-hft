#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <string>

namespace postrade::msg
{
  struct AddTradeRecord : public actors::Message_N<180>
  {
        unsigned long long epoch;
        std::string venue;
        std::string symbol;
        std::string sector;
        std::string exid1;
        std::string exid2;
        std::string clid;
        std::string side;
        double qty;
        std::string trader;
        std::string counterparty1;
        std::string counterparty2;
        std::string cusip;
        double price;
        int chid;

        AddTradeRecord(
            unsigned long long epoch,
            const std::string &venue,
            const std::string &symbol,
            const std::string &sector,
            const std::string &exid1,
            const std::string &exid2,
            const std::string &clid,
            const std::string &side,
            double qty,
            const std::string &trader,
            const std::string &counterparty1,
            const std::string &counterparty2,
            const std::string &cusip,
            double price,
            int chid)
            : epoch(epoch),
              venue(venue),
              symbol(symbol),
              sector(sector),
              exid1(exid1),
              exid2(exid2),
              clid(clid),
              side(side),
              qty(qty),
              trader(trader),
              counterparty1(counterparty1),
              counterparty2(counterparty2),
              cusip(cusip),
              price(price),
              chid(chid)
        {
        }
  };;
}
