#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include "frame/mda/msg/Data.hpp"

namespace frame::ob::msg
{
  struct EndOfBurst : public  actors::Message_N<75> , public actors::MemoryPool<EndOfBurst, 256, 16, 4096>
  {

    using payload_ptr_t = boost::intrusive_ptr<const frame::mda::msg::data_pay_load>;

    payload_ptr_t payload=nullptr;
    bool last = true;

    EndOfBurst() = default;

    EndOfBurst(
      payload_ptr_t _payload)
      : payload(_payload)  {}

    friend std::ostream& operator<<(std::ostream& os, const EndOfBurst& e) {
      os << '{';
      os << "\"payload_set\":" << (e.payload ? "true" : "false");
      if (e.payload) {
      os << ",\"payload\":";
      os << e.payload->as_json();
      } else {
      os << ",\"payload\":null";
      }
      os << '}';
      return os;
    }

    bool operator==(const EndOfBurst& other) const {
      if (!payload && !other.payload) return true;
      if (!payload || !other.payload) return false;
      return *payload == *other.payload;
    }

  };
}
