#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include "mcast_recv/message_buffer.hpp"

namespace mcast_recv::msg
{
  template <typename seqnumT>
  struct ProcessQ : public actors::MessageT<ProcessQ<seqnumT>>, public actors::MemoryPool<ProcessQ<seqnumT>,16,16,4096>
  {
    // NOTE: handlers receive `const ProcessQ*`, so MsgBuf::processq_handler
    // const_casts to write buf.qlen. Marking this `mutable` would remove that
    // cast, matching how actors::Message::qlen is declared. Not done here
    // because message_buffer is ~2 KB and `mutable` on an aggregate that large
    // invites writes from other const handlers; the single documented cast in
    // MsgBuf is the narrower hole. Revisit if a second writer appears.
    message_buffer buf;
  };
}
