#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "frame/mda/msg/Data.hpp"
#include <iostream>

namespace frame::ob::msg
{
  struct TradeNotify : public actors::Message_N<76>  , public actors::MemoryPool<TradeNotify,64,16,2048>
  {

    using payload_ptr_t = boost::intrusive_ptr<const frame::mda::msg::data_pay_load>;

    payload_ptr_t payload;

    TradeNotify() : payload(nullptr) {}

    TradeNotify(
        payload_ptr_t payload)
        : payload(payload) {}

    inline bool operator==(const TradeNotify& other) const
    {
      return payload && other.payload && payload->data_only_equals(*other.payload);
    }

    inline friend std::ostream& operator<<(std::ostream& out, const TradeNotify& m)
    {
      if (m.payload)
        return out << "TradeNotify: " << *m.payload;
      else
        return out << "TradeNotify: null payload";
    }
  };
}