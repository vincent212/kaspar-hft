/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KASPR: QLen factory function

#include "frame/qlen/act/QLen.hpp"
#include "interface/frame/qlen/if/QLen.hpp"

actor_ptr create_QLen(actors::Manager *man,
                      actor_ptr binrec,
                      int period_ms,
                      int ctx_every,
                      const std::vector<std::string> &prefixes)
{
    return new frame::qlen::act::QLen(man, binrec, period_ms, ctx_every, prefixes);
}
