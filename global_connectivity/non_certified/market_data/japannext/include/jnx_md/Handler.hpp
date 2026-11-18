#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Handler: one MoldUDP64 stream of Japannext ITCH, with its recovery.
//
//   MoldUDP64 packets -> sequencer -> Dispatcher -> Sink (OB books, recorder)
//                          |   ^
//            re-request    |   | GLIMPSE snapshot (SoupBinTCP bytes)
//                          v   |
//                         hooks (the actor sends them; the replay simulates them)
//
// Sequencing follows MoldUDP64 1.02, 2.6.3 (feed.md section 5):
//  - a packet's first number above the expected one is a gap: the packet is
//    buffered and the missing range re-requested; packets that close the hole
//    are applied, then the buffer is drained in order;
//  - a packet wholly below the expected number is a duplicate; one that
//    straddles it is applied from the first new message;
//  - heartbeats (count 0) and End of Session (count 0xFFFF) carry the next
//    expected number, so they reveal a gap too;
//  - a gap that re-request has not closed within the timeout (or with no
//    re-request server) sends l3_gap_v2_t to every book and starts a GLIMPSE
//    snapshot: l3_chr_v2_t to every book, state cleared, packets buffered;
//  - End of Snapshot gives the next sequence number: buffered messages below it
//    are dropped, the rest applied (GLIMPSE 5.8); a hole left between it and
//    the buffer is re-requested, or another snapshot taken;
//  - a new session (a new trading day), or End of Session, resets the channel.
//
// Single-threaded: the owner calls every method from one thread.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "jnx_md/Dispatcher.hpp"
#include "jnx_md/Instruments.hpp"
#include "jnx_md/Types.hpp"

namespace jnx_md
{
  class Handler
  {
  public:
    // What to do with the first packet of a session.
    enum class Join
    {
      FirstPacket,   // start at it (replay of a capture that begins before trading)
      FromOne,       // expect sequence 1: re-request what is missing
      Snapshot       // buffer and take a GLIMPSE snapshot (late join)
    };

    enum class Mode
    {
      Live,        // in sequence
      Buffering,   // gap: waiting for re-requested messages
      Snapshot,    // waiting for a GLIMPSE snapshot
      Ended        // End of Session seen
    };

    struct Config
    {
      Dispatcher::Config dispatch;
      Instruments::Config instruments;
      Join join = Join::Snapshot;
      uint64_t rerequest_timeout_ns = 2000000000ull;   // then a snapshot
      uint32_t rerequest_max = 0xFFFE;                 // messages per request
    };

    struct Hooks
    {
      // Send a MoldUDP64 Request Packet; false if there is no re-request server.
      std::function<bool(std::string_view session, uint64_t seq, uint32_t count)> rerequest;
      // Open a GLIMPSE session; its bytes come back through on_glimpse_bytes.
      std::function<void()> request_snapshot;
      // When set, called on the first packet and on every new session: the
      // UTC ns of JST midnight of the session date (timestamps count from it).
      std::function<uint64_t()> session_midnight;
    };

    Handler(const Config &cfg, Sink &sink, Hooks hooks = {});

    // One MoldUDP64 datagram; recv_ns is the receive (or capture) time.
    void on_packet(const char *data, size_t len, uint64_t recv_ns);
    // Bytes from the GLIMPSE server (SoupBinTCP), in order, in any chunks.
    void on_glimpse_bytes(const char *data, size_t len);
    // The GLIMPSE connection closed or failed before End of Snapshot.
    void glimpse_failed(const char *why);
    // Timeouts; call now and then with the current time.
    void tick(uint64_t now_ns);
    // Reset the channel now and recover by snapshot (an operator command, or
    // an injected reset in a test).
    void force_recovery(const char *why);

    // Load the directory, tick tables and trading states from a GLIMPSE byte
    // stream without taking it as a snapshot of the stream: no reset, no
    // alignment, and the time it carries is forgotten. For a replay whose
    // capture starts after the day's directory was sent.
    void preload_directory(const char *data, size_t len);

    Mode mode() const { return mode_; }
    uint64_t next_seq() const { return next_; }
    const std::string &session() const { return session_; }
    size_t buffered() const { return buffer_.size(); }
    const Stats &stats() const { return st_; }
    const Dispatcher &dispatcher() const { return disp_; }
    Instruments &instruments() { return inst_; }
    const Instruments &instruments() const { return inst_; }

  private:
    struct Packet
    {
      std::string bytes;
      uint64_t first = 0;
      uint16_t count = 0;
    };

    Config cfg_;
    Hooks hooks_;
    Stats st_;
    Instruments inst_;
    Dispatcher disp_;

    std::string session_;
    bool have_session_ = false;
    Mode mode_ = Mode::Live;
    uint64_t next_ = 0;                   // next expected sequence number; 0 unknown
    std::map<uint64_t, Packet> buffer_;   // by first sequence number
    uint64_t gap_since_ns_ = 0, now_ns_ = 0, requested_from_ = 0, hole_upto_ = 0;

    std::string soup_;                    // GLIMPSE bytes not yet decoded
    bool preloading_ = false;

    void join(uint64_t seq);
    void new_session(const std::string &s, uint64_t seq);
    void buffer(const char *data, size_t len, uint64_t first, uint16_t count);
    void apply(const char *data, size_t len, uint64_t first, uint16_t count);
    void drain();
    void hole(uint64_t upto);
    void rerequest();
    void escalate(const char *why);
    void start_snapshot();
    void end_snapshot(uint64_t seq);
    void glimpse_message(const char *p, size_t n);
  };
}
