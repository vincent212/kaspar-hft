#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// GlimpseClient: one GLIMPSE session over SoupBinTCP per SnapshotRequest, on its
// own thread (a blocking TCP session). Logs in with a blank Requested Session
// (GLIMPSE requires it) and sequence 0, forwards every byte read to the feed
// as GlimpseData, sends a Client Heartbeat after a second without sending
// (SoupBinTCP 4.3), and logs out after End of Snapshot (GLIMPSE 5.8). A
// transport failure (no connection, a closed socket, 15 s of silence) is
// reported as GlimpseFailed. A Login Rejected or an End of Session before End
// of Snapshot ends the session quietly: the feed sees those packets in the
// bytes it decodes and counts the failure there, once. Not run against the
// venue.

#include <cstdint>
#include <string>

#include "actors/Actor.hpp"
#include "jnx_md/msg/Glimpse.hpp"

namespace jnx_md::act
{
  struct GlimpseConfig
  {
    std::string name = "JNX";
    std::string host;              // IPv4 address of the GLIMPSE server
    uint16_t port = 0;
    std::string username, password;
  };

  class GlimpseClient : public actors::Actor
  {
  public:
    GlimpseClient(const GlimpseConfig &cfg, actors::Actor *feed);
    const char *get_name() const override { return name_.c_str(); }

  private:
    GlimpseConfig cfg_;
    std::string name_;
    actors::Actor *feed_;

    void request_handler(const msg::SnapshotRequest *) noexcept;
    // One session; returns an empty string on End of Snapshot, else why not.
    std::string session();
  };
}
