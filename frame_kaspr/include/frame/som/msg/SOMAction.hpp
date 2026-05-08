#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include "enum/som_code.hpp"
#include "enum/exch_code.hpp"
#include "enum/buy_sell.hpp"

namespace frame::som::msg
{
  struct SOMAction : public actors::Message_N<251> , public actors::MemoryPool<SOMAction>
  {
    en::som som_code;
    uint32_t ordid;
    en::x venue;
    int sym_id;
    en::bs side;
    int32_t px;
    int32_t sz;
    std::string sender;

    SOMAction(
      en::som som_code_,
      uint32_t ordid_,
      int32_t sz_ = 0,
      en::x venue_ = en::x::UNI,
      int sym_id_ = 0,
      en::bs side_ = en::bs::UNI,
      int32_t px_ = 0,
      const std::string &sender_ = ""
    )
      : som_code(som_code_),
      ordid(ordid_),
      venue(venue_),
      sym_id(sym_id_),
      side(side_),
      px(px_),
      sz(sz_),
      sender(sender_)
    {}

    virtual ~SOMAction() {}
  };

}
