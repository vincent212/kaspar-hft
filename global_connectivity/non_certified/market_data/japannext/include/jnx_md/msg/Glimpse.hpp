#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Messages between the JnxFeed actor and the GlimpseClient actor.

#include <string>

#include "actors/Message.hpp"

namespace jnx_md::msg
{
  // Feed -> client: open a GLIMPSE session and stream it back.
  struct SnapshotRequest : public actors::MessageT<SnapshotRequest>
  {
  };

  // Client -> feed: SoupBinTCP bytes as read from the GLIMPSE server.
  struct GlimpseData : public actors::MessageT<GlimpseData>
  {
    std::string bytes;
    explicit GlimpseData(std::string b = {}) : bytes(std::move(b)) {}
  };

  // Client -> feed: the session ended before End of Snapshot, or could not open.
  struct GlimpseFailed : public actors::MessageT<GlimpseFailed>
  {
    std::string why;
    explicit GlimpseFailed(std::string w = {}) : why(std::move(w)) {}
  };
}
