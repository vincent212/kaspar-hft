#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// JnxFeed: the actor around one Handler (one MoldUDP64 stream).
//
//   mcast_recv::SocketReader<uint64_t, 10>   (or PCAPReader<uint64_t, 10>)
//        | ProcessQ<uint64_t>: one MoldUDP64 datagram
//        v
//   JnxFeed -- Handler -- Data (l3 records) --> the instrument's OB / TachBook
//        |  ^                              \--> binrec (every record, and EOP)
//        |  | GlimpseData / GlimpseFailed
//        v  |
//   GlimpseClient (its own thread: a blocking TCP session)
//
// Re-requests (MOLD 4) go from a UDP socket of this actor to the re-request
// server; the replies come back to that socket, which is read without
// blocking on every packet and every timer tick while a gap is open.
//
// Books are found with a BookFor callback the first time an instrument is
// listed (as BFA's factory does): it returns the OB for the instrument, or
// nullptr to leave it unbooked. Not run against the venue.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Timeout.hpp"
#include "jnx_md/Handler.hpp"
#include "jnx_md/msg/Glimpse.hpp"
#include "mcast_recv/msg/ProcessQ.hpp"

namespace jnx_md::act
{
  struct FeedConfig
  {
    std::string name = "JNX";
    Handler::Config handler;
    std::string rerequest_host;    // MoldUDP64 re-request server; empty = none
    uint16_t rerequest_port = 0;
    bool fast_send = true;         // books on the same thread (fast_send) or not
  };

  class JnxFeed : public actors::Actor
  {
  public:
    using BookFor = std::function<actors::Actor *(const Instrument &)>;

    JnxFeed(const FeedConfig &cfg, BookFor book_for, actors::Actor *binrec);
    ~JnxFeed() override;

    void set_glimpse(actors::Actor *client) { glimpse_ = client; }
    const Handler &handler() const { return handler_; }
    const char *get_name() const override { return name_.c_str(); }

  private:
    struct Out : Sink
    {
      JnxFeed &f;
      explicit Out(JnxFeed &feed) : f(feed) {}
      void book(int32_t asset, const bfile::l3_t &r) override;
      void all_books(const bfile::l3_t &r) override;
      void recorder(const bfile::l3_t &r) override;
    };

    FeedConfig cfg_;
    std::string name_;
    BookFor book_for_;
    actors::Actor *binrec_;
    actors::Actor *glimpse_ = nullptr;
    Out out_;
    Handler handler_;
    std::unordered_map<int32_t, actors::Actor *> books_;   // nullptr: no book wanted
    std::vector<actors::Actor *> all_;
    int rr_sock_ = -1;

    void start_handler(const actors::msg::Start *) noexcept;
    void packet_handler(const mcast_recv::msg::ProcessQ<uint64_t> *m) noexcept;
    void glimpse_handler(const msg::GlimpseData *m) noexcept;
    void glimpse_failed_handler(const msg::GlimpseFailed *m) noexcept;
    void timeout_handler(const actors::msg::Timeout *) noexcept;

    actors::Actor *book_of(int32_t asset);
    void send_to(actors::Actor *book, const bfile::l3_t &r);
    bool rerequest(std::string_view session, uint64_t seq, uint32_t count);
    void poll_replies();
    uint64_t now_ns() const;
  };
}
