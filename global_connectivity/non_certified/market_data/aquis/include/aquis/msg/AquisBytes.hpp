#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>

#include "actors/Message.hpp"

namespace aquis::msg
{

// Bytes for AquisFeed from whatever reads the network: one UDP payload of the
// real-time or snapshot feed, or a piece of the replay TCP stream.
struct AquisBytes : public actors::MessageT<AquisBytes>
{
  enum class Source
  {
    live,         // real-time multicast, A or B
    snapshot,     // snapshot multicast
    replay,       // replay TCP stream
    replay_lost,  // the replay connection failed or closed; data is empty
  };

  Source source = Source::live;
  std::string data;

  AquisBytes() = default;
  AquisBytes(Source s, std::string d) : source(s), data(std::move(d)) {}
};

} // namespace aquis::msg
