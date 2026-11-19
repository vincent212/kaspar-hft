/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "jnx_md/act/JnxFeed.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>

#include "actors/act/Timer.hpp"
#include "frame/mda/msg/Data.hpp"
#include "jnx_md/gen/moldudp64.hpp"

namespace jnx_md::act
{
  JnxFeed::JnxFeed(const FeedConfig &cfg, BookFor book_for, actors::Actor *binrec)
      : cfg_(cfg), name_(cfg.name + "Feed"), book_for_(std::move(book_for)), binrec_(binrec),
        out_(*this),
        handler_(cfg.handler, out_,
                 Handler::Hooks{
                     [this](std::string_view s, uint64_t seq, uint32_t n) {
                       return rerequest(s, seq, n);
                     },
                     [this] {
                       if (glimpse_)
                         glimpse_->send(new msg::SnapshotRequest(), this);
                     },
                     // A configured session_date wins; otherwise the clock.
                     cfg.handler.dispatch.midnight_ns
                         ? std::function<uint64_t()>()
                         : std::function<uint64_t()>(
                               [this] { return jst_midnight_ns(session_date(now_ns())); })})
  {
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(mcast_recv::msg::ProcessQ<uint64_t>, packet_handler);
    MESSAGE_HANDLER(msg::GlimpseData, glimpse_handler);
    MESSAGE_HANDLER(msg::GlimpseFailed, glimpse_failed_handler);
    MESSAGE_HANDLER(actors::msg::Timeout, timeout_handler);
  }

  JnxFeed::~JnxFeed()
  {
    if (rr_sock_ >= 0)
      ::close(rr_sock_);
  }

  uint64_t JnxFeed::now_ns() const
  {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());
  }

  void JnxFeed::start_handler(const actors::msg::Start *) noexcept
  {
    if (!cfg_.rerequest_host.empty() && cfg_.rerequest_port)
    {
      rr_sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
      if (rr_sock_ >= 0)
        ::fcntl(rr_sock_, F_SETFL, ::fcntl(rr_sock_, F_GETFL) | O_NONBLOCK);
    }
    actors::act::Timer::wake_up_in(this, 0, 250);
  }

  void JnxFeed::packet_handler(const mcast_recv::msg::ProcessQ<uint64_t> *m) noexcept
  {
    const auto &b = m->buf;
    handler_.on_packet(b.message.data(), b.len, b.recv_ts ? b.recv_ts : now_ns());
    if (handler_.mode() == Handler::Mode::Buffering)
      poll_replies();
  }

  void JnxFeed::glimpse_handler(const msg::GlimpseData *m) noexcept
  {
    handler_.on_glimpse_bytes(m->bytes.data(), m->bytes.size());
  }

  void JnxFeed::glimpse_failed_handler(const msg::GlimpseFailed *m) noexcept
  {
    handler_.glimpse_failed(m->why.c_str());
  }

  void JnxFeed::timeout_handler(const actors::msg::Timeout *) noexcept
  {
    if (handler_.mode() == Handler::Mode::Buffering)
      poll_replies();
    handler_.tick(now_ns());
    actors::act::Timer::wake_up_in(this, 0, 250);
  }

  bool JnxFeed::rerequest(std::string_view session, uint64_t seq, uint32_t count)
  {
    if (rr_sock_ < 0)
      return false;
    jnx::moldudp64::to_jnx::RequestPacket r{};
    std::memset(r.Session, ' ', sizeof(r.Session));
    std::memcpy(r.Session, session.data(), std::min(session.size(), sizeof(r.Session)));
    r.SequenceNumber = seq;
    r.RequestedMessageCount = uint16_t(std::min<uint32_t>(count, 0xFFFE));
    char buf[sizeof(r)];
    const size_t n = jnx::moldudp64::to_jnx::encode(buf, sizeof(buf), r);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(cfg_.rerequest_port);
    if (::inet_pton(AF_INET, cfg_.rerequest_host.c_str(), &to.sin_addr) != 1)
      return false;
    return ::sendto(rr_sock_, buf, n, 0, reinterpret_cast<sockaddr *>(&to), sizeof(to)) ==
           ssize_t(n);
  }

  void JnxFeed::poll_replies()
  {
    if (rr_sock_ < 0)
      return;
    char buf[65536];
    for (;;)
    {
      const ssize_t n = ::recv(rr_sock_, buf, sizeof(buf), 0);
      if (n <= 0)
        return;
      handler_.on_packet(buf, size_t(n), now_ns());
    }
  }

  actors::Actor *JnxFeed::book_of(int32_t asset)
  {
    auto it = books_.find(asset);
    if (it != books_.end())
      return it->second;
    actors::Actor *b = nullptr;
    if (const Instrument *i = handler_.instruments().by_asset(asset); i && book_for_)
      b = book_for_(*i);
    books_[asset] = b;
    if (b)
      all_.push_back(b);
    return b;
  }

  void JnxFeed::send_to(actors::Actor *book, const bfile::l3_t &r)
  {
    if (cfg_.fast_send)
    {
      frame::mda::msg::Data d(true);
      d.l3 = r;
      book->fast_send(d, this);
    }
    else
    {
      auto *d = new frame::mda::msg::Data(true);
      d->l3 = r;
      book->send(d, this);
    }
  }

  void JnxFeed::Out::book(int32_t asset, const bfile::l3_t &r)
  {
    if (auto *b = f.book_of(asset))
      f.send_to(b, r);
    recorder(r);
  }

  void JnxFeed::Out::all_books(const bfile::l3_t &r)
  {
    for (auto *b : f.all_)
      f.send_to(b, r);
    recorder(r);
  }

  void JnxFeed::Out::recorder(const bfile::l3_t &r)
  {
    if (!f.binrec_)
      return;
    auto *d = new frame::mda::msg::Data(true);
    d->l3 = r;
    f.binrec_->send(d, &f);
  }
}
