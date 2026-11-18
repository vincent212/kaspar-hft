/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "nasdaq_ouch/SoupSession.hpp"

#include <cstdlib>
#include <cstring>

#include "nasdaq_ouch/gen/soupbintcp.hpp"

namespace nasdaq::ouch
{
  namespace asio = boost::asio;
  using asio::ip::tcp;
  namespace sin = nasdaq::soup::from_nasdaq;
  namespace sout = nasdaq::soup::to_nasdaq;

  namespace
  {
    constexpr size_t MAX_PACKET = 2 + 65535;
    constexpr int TICK_MS = 100;
  }

  SoupSession::SoupSession(const SessionConfig &cfg, SessionEvents &ev)
      : cfg_(cfg), ev_(ev), work_(asio::make_work_guard(io_)), resolver_(io_), sock_(io_), timer_(io_),
        rbuf_(2 * MAX_PACKET), thr_([this] { io_.run(); })
  {
  }

  SoupSession::~SoupSession()
  {
    // Handlers already posted (a logout) run first; closing cancels the
    // socket and timer operations, whose handlers then see dead_ and return,
    // and run() ends when nothing is left.
    asio::post(io_, [this] {
      dead_ = true;
      close();
    });
    work_.reset();
    if (thr_.joinable())
      thr_.join();
  }

  // ---- calls from the owner's thread -------------------------------------------------------

  void SoupSession::logon(const LogonParams &p)
  {
    asio::post(io_, [this, p] {
      logon_ = p;
      do_connect();
    });
  }

  CallStatus SoupSession::send_packet(std::string packet)
  {
    if (!logged_in_)
      return CallStatus::Disconnected;
    if (busy_ || queued_ + packet.size() > cfg_.max_queued_bytes)
    {
      busy_ = true;
      return CallStatus::Busy;
    }
    queued_ += packet.size();
    asio::post(io_, [this, packet = std::move(packet)]() mutable { queue_write(std::move(packet), true); });
    return CallStatus::Success;
  }

  // One OUCH request in an Unsequenced Data packet (SoupBinTCP 3.0 2.3.2).
  CallStatus SoupSession::send_ouch(std::string_view ouch)
  {
    std::string p(sizeof(sout::UnsequencedData) + ouch.size(), '\0');
    if (!sout::encode(p.data(), p.size(), sout::UnsequencedData{}, ouch))
      return CallStatus::Disconnected; // cannot happen: OUCH requests are far below 64 KB
    return send_packet(std::move(p));
  }

  CallStatus SoupSession::enter(const to_nasdaq::EnterOrder &m, std::string_view appendage)
  {
    auto o = m;
    o.AppendageLength = uint16_t(appendage.size());
    char buf[sizeof(o) + 256];
    size_t n = to_nasdaq::encode(buf, sizeof(buf), o, appendage);
    if (!n)
      return CallStatus::Disconnected;
    return send_ouch(std::string_view(buf, n));
  }

  CallStatus SoupSession::cancel(const to_nasdaq::CancelOrderRequest &m)
  {
    auto o = m;
    o.AppendageLength = 0;
    char buf[sizeof(o)];
    size_t n = to_nasdaq::encode(buf, sizeof(buf), o, std::string_view());
    return send_ouch(std::string_view(buf, n));
  }

  CallStatus SoupSession::account_query()
  {
    to_nasdaq::AccountQueryRequest o{};
    o.AppendageLength = 0;
    char buf[sizeof(o)];
    size_t n = to_nasdaq::encode(buf, sizeof(buf), o, std::string_view());
    return send_ouch(std::string_view(buf, n));
  }

  void SoupSession::logout()
  {
    asio::post(io_, [this] {
      bool was = connected_ && !dead_;
      dead_ = true; // no events after a logout
      logged_in_ = false;
      if (was)
      {
        char buf[sizeof(sout::LogoutRequest)];
        size_t n = sout::encode(buf, sizeof(buf), sout::LogoutRequest{});
        boost::system::error_code ec;
        asio::write(sock_, asio::buffer(buf, n), ec);
      }
      close();
    });
  }

  // ---- I/O thread ----------------------------------------------------------------------------

  void SoupSession::close()
  {
    connected_ = false;
    logged_in_ = false;
    boost::system::error_code ec;
    timer_.cancel();
    resolver_.cancel();
    sock_.shutdown(tcp::socket::shutdown_both, ec);
    sock_.close(ec);
  }

  void SoupSession::fail(const std::string &why)
  {
    if (dead_)
      return;
    dead_ = true;
    close();
    ev_.disconnected(why);
  }

  void SoupSession::do_connect()
  {
    started_ = last_recv_ = last_send_ = Clock::now();
    tick();
    resolver_.async_resolve(cfg_.host, cfg_.port, [this](const boost::system::error_code &ec, tcp::resolver::results_type r) {
      if (dead_)
        return;
      if (ec)
        return fail("resolve " + cfg_.host + ":" + cfg_.port + ": " + ec.message());
      asio::async_connect(sock_, r, [this](const boost::system::error_code &ec, const tcp::endpoint &) {
        if (dead_)
          return;
        if (ec)
          return fail("connect: " + ec.message());
        connected_ = true;
        boost::system::error_code ignore;
        sock_.set_option(tcp::no_delay(true), ignore);
        last_recv_ = Clock::now();
        // Login Request (SoupBinTCP 3.0 2.3.1).
        sout::LoginRequest l{};
        l.set_Username(logon_.username);
        l.set_Password(logon_.password);
        l.set_RequestedSession(logon_.session);
        l.set_RequestedSequenceNumber(std::to_string(logon_.sequence));
        std::string p(sizeof(l), '\0');
        sout::encode(p.data(), p.size(), l);
        queue_write(std::move(p));
        do_read();
      });
    });
  }

  void SoupSession::tick()
  {
    timer_.expires_after(std::chrono::milliseconds(TICK_MS));
    timer_.async_wait([this](const boost::system::error_code &ec) {
      if (ec || dead_)
        return;
      auto now = Clock::now();
      if (!connected_ && now - started_ > std::chrono::milliseconds(cfg_.connect_timeout_ms))
        return fail("connect timed out");
      if (connected_ && now - last_recv_ > std::chrono::milliseconds(cfg_.receive_timeout_ms))
        return fail("nothing received for " + std::to_string(cfg_.receive_timeout_ms) + " ms");
      if (login_done_ && !writing_ && now - last_send_ >= std::chrono::milliseconds(cfg_.heartbeat_ms))
      {
        // Client Heartbeat (SoupBinTCP 3.0 2.3.3).
        std::string p(sizeof(sout::ClientHeartbeat), '\0');
        sout::encode(p.data(), p.size(), sout::ClientHeartbeat{});
        queue_write(std::move(p));
      }
      tick();
    });
  }

  void SoupSession::queue_write(std::string packet, bool counted)
  {
    if (dead_)
      return;
    wq_.push_back({std::move(packet), counted});
    if (!writing_)
      do_write();
  }

  void SoupSession::do_write()
  {
    if (wq_.empty() || dead_)
    {
      writing_ = false;
      return;
    }
    writing_ = true;
    asio::async_write(sock_, asio::buffer(wq_.front().first), [this](const boost::system::error_code &ec, size_t n) {
      if (dead_)
        return;
      if (ec)
        return fail("write: " + ec.message());
      last_send_ = Clock::now();
      // Only requests from send_packet are counted in queued_; the session's
      // own packets (login, heartbeat) are not.
      if (wq_.front().second)
        queued_ -= n;
      wq_.pop_front();
      if (busy_ && queued_ <= cfg_.max_queued_bytes / 2)
      {
        busy_ = false;
        ev_.available();
      }
      do_write();
    });
  }

  void SoupSession::do_read()
  {
    if (rlen_ == rbuf_.size())
      return fail("receive buffer full"); // cannot happen: it holds two maximal packets
    sock_.async_read_some(asio::buffer(rbuf_.data() + rlen_, rbuf_.size() - rlen_),
                          [this](const boost::system::error_code &ec, size_t n) {
                            if (dead_)
                              return;
                            if (ec)
                              return fail(ec == asio::error::eof ? "closed by the server" : "read: " + ec.message());
                            rlen_ += n;
                            last_recv_ = Clock::now();
                            on_data();
                            if (!dead_)
                              do_read();
                          });
  }

  void SoupSession::on_data()
  {
    size_t off = 0;
    while (!dead_ && off < rlen_)
    {
      auto d = sin::decode(rbuf_.data() + off, rlen_ - off);
      if (d.st == nasdaq::status::incomplete)
        break;
      if (d.st == nasdaq::status::unknown || d.st == nasdaq::status::malformed)
      {
        if (d.size == 0)
          return fail("unreadable SoupBinTCP packet");
        off += d.size; // skip it: the length prefix frames it
        continue;
      }
      off += d.size;
      std::visit(
          [&](const auto &m) {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, sin::LoginAccepted>)
            {
              std::string s(m.get_SequenceNumber());
              seq_ = std::strtoull(s.c_str(), nullptr, 10);
              login_done_ = true;
              logged_in_ = true;
              ev_.logged_in(std::string(m.get_Session()), seq_);
            }
            else if constexpr (std::is_same_v<T, sin::LoginRejected>)
            {
              dead_ = true; // the server closes the socket; that is not a disconnect
              close();
              ev_.login_rejected(m.RejectReasonCode);
            }
            else if constexpr (std::is_same_v<T, sin::SequencedData>)
            {
              if (!login_done_)
                return fail("Sequenced Data before Login Accepted");
              ev_.message(seq_++, d.tail, d.tail_len);
            }
            else if constexpr (std::is_same_v<T, sin::EndOfSession>)
            {
              dead_ = true;
              close();
              ev_.end_of_session();
            }
            // ServerHeartbeat: last_recv_ is enough. Debug: ignored.
          },
          d.msg);
    }
    if (off)
    {
      std::memmove(rbuf_.data(), rbuf_.data() + off, rlen_ - off);
      rlen_ -= off;
    }
  }

  SessionFactory soup_session_factory()
  {
    return [](const SessionConfig &cfg, SessionEvents &ev) -> std::unique_ptr<OuchSession> {
      return std::make_unique<SoupSession>(cfg, ev);
    };
  }
}
