/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "currenex_ust/TcpSession.hpp"

#include <cstring>

namespace currenex::ouch
{
  namespace asio = boost::asio;
  using asio::ip::tcp;

  namespace
  {
    constexpr size_t RBUF = 1 << 16;
    constexpr int TICK_MS = 100;

    // The SessionID sits right after the type code in every request that
    // has one (E and G, UST OUCH 10.4 and 10.7).
    void set_session_id(std::string &msg, int32_t id)
    {
      be<int32_t> v;
      v = id;
      std::memcpy(msg.data() + 1, &v, sizeof(v));
    }
  }

  TcpSession::TcpSession(const SessionConfig &cfg, SessionEvents &ev)
      : cfg_(cfg), ev_(ev), work_(asio::make_work_guard(io_)), resolver_(io_), sock_(io_), timer_(io_),
        rbuf_(RBUF), thr_([this] { io_.run(); })
  {
  }

  TcpSession::~TcpSession()
  {
    asio::post(io_, [this] {
      dead_ = true;
      close();
    });
    work_.reset();
    if (thr_.joinable())
      thr_.join();
  }

  // ---- calls from the owner's thread -------------------------------------------------------

  void TcpSession::logon(const LogonParams &p)
  {
    asio::post(io_, [this, p] {
      logon_ = p;
      user_ = p.user;
      do_connect();
    });
  }

  CallStatus TcpSession::send_request(std::string msg, bool needs_session_id)
  {
    if (!logged_in_)
      return CallStatus::Disconnected;
    size_t n = msg.size() + FRAME_OVERHEAD;
    if (busy_ || queued_ + n > cfg_.max_queued_bytes)
    {
      busy_ = true;
      return CallStatus::Busy;
    }
    queued_ += n;
    asio::post(io_, [this, msg = std::move(msg), needs_session_id]() mutable {
      if (needs_session_id)
        set_session_id(msg, session_id_);
      send_now(std::move(msg), true);
    });
    return CallStatus::Success;
  }

  CallStatus TcpSession::new_order(const to::NewOrder &m) { return send_request(encoded(m), false); }
  CallStatus TcpSession::cancel(const to::OrderCancelRequest &m) { return send_request(encoded(m), false); }
  CallStatus TcpSession::instrument_info_request() { return send_request(encoded(to::InstrumentInfoRequest{}), true); }
  CallStatus TcpSession::routing_user_info_request() { return send_request(encoded(to::RoutingUserInfoRequest{}), true); }

  CallStatus TcpSession::resend(uint32_t begin)
  {
    to::ResendRequest r{};
    r.BeginSeqNo = int32_t(begin);
    return send_request(encoded(r), false);
  }

  void TcpSession::logout()
  {
    asio::post(io_, [this] {
      bool was = connected_ && !dead_;
      dead_ = true; // no events after a logout
      logged_in_ = false;
      if (was)
      {
        to::Logout l{};
        l.set_UserID(user_);
        l.SessionID = session_id_.load();
        l.set_Reason("A6"); // User Logout (Appendix B)
        auto f = to_frame(next_out_++, ms_of_day(epoch_ms_now()), encoded(l));
        boost::system::error_code ec;
        asio::write(sock_, asio::buffer(f), ec);
      }
      close();
    });
  }

  // ---- I/O thread ----------------------------------------------------------------------------

  void TcpSession::close()
  {
    connected_ = false;
    logged_in_ = false;
    boost::system::error_code ec;
    timer_.cancel();
    resolver_.cancel();
    sock_.shutdown(tcp::socket::shutdown_both, ec);
    sock_.close(ec);
  }

  void TcpSession::fail(const std::string &why)
  {
    if (dead_)
      return;
    dead_ = true;
    close();
    ev_.disconnected(why);
  }

  void TcpSession::do_connect()
  {
    started_ = last_recv_ = Clock::now();
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
        connected_at_ = last_recv_ = Clock::now();
        // Logon, always sequence number 1 (UST OUCH 5.1.1, p.8).
        to::Logon l{};
        l.set_UserID(logon_.user);
        l.set_Password(logon_.password);
        l.SessionID = 0;
        l.Version = logon_.version;
        next_out_ = 1;
        send_now(encoded(l));
        do_read();
      });
    });
  }

  void TcpSession::tick()
  {
    timer_.expires_after(std::chrono::milliseconds(TICK_MS));
    timer_.async_wait([this](const boost::system::error_code &ec) {
      if (ec || dead_)
        return;
      auto now = Clock::now();
      if (!connected_ && now - started_ > std::chrono::milliseconds(cfg_.connect_timeout_ms))
        return fail("connect timed out");
      if (connected_ && !login_done_ && now - connected_at_ > std::chrono::milliseconds(cfg_.logon_timeout_ms))
        return fail("no Logon response");
      if (connected_ && now - last_recv_ > std::chrono::milliseconds(cfg_.receive_timeout_ms))
        return fail("nothing received for " + std::to_string(cfg_.receive_timeout_ms) + " ms");
      tick();
    });
  }

  // Frames msg with the next outbound sequence number and queues it.
  void TcpSession::send_now(std::string msg, bool counted)
  {
    if (dead_)
    {
      if (counted)
        queued_ -= msg.size() + FRAME_OVERHEAD;
      return;
    }
    auto f = to_frame(next_out_++, ms_of_day(epoch_ms_now()), msg);
    size_t n = counted ? f.size() : 0;
    wq_.push_back({std::move(f), n});
    if (!writing_)
      do_write();
  }

  void TcpSession::do_write()
  {
    if (wq_.empty() || dead_)
    {
      writing_ = false;
      return;
    }
    writing_ = true;
    asio::async_write(sock_, asio::buffer(wq_.front().first), [this](const boost::system::error_code &ec, size_t) {
      if (dead_)
        return;
      if (ec)
        return fail("write: " + ec.message());
      queued_ -= wq_.front().second;
      wq_.pop_front();
      if (busy_ && queued_ <= cfg_.max_queued_bytes / 2)
      {
        busy_ = false;
        ev_.available();
      }
      do_write();
    });
  }

  void TcpSession::do_read()
  {
    if (rlen_ == rbuf_.size())
      return fail("receive buffer full");
    sock_.async_read_some(asio::buffer(rbuf_.data() + rlen_, rbuf_.size() - rlen_),
                          [this](const boost::system::error_code &ec, size_t n) {
                            if (dead_)
                              return;
                            if (ec)
                              return fail(ec == asio::error::eof ? "closed by Currenex" : "read: " + ec.message());
                            rlen_ += n;
                            last_recv_ = Clock::now();
                            on_data();
                            if (!dead_)
                              do_read();
                          });
  }

  void TcpSession::on_data()
  {
    size_t off = 0;
    while (!dead_ && off < rlen_)
    {
      auto f = deframe(rbuf_.data() + off, rlen_ - off);
      if (f.st == Frame::incomplete)
        break;
      off += f.consumed;
      if (f.st == Frame::garbage)
        continue; // bytes outside a frame: skipped
      if (f.st == Frame::unknown)
      {
        if (login_done_)
          ev_.undecoded(uint32_t(f.seq), f.type);
        continue;
      }
      on_frame(f);
    }
    if (off)
    {
      std::memmove(rbuf_.data(), rbuf_.data() + off, rlen_ - off);
      rlen_ -= off;
    }
  }

  void TcpSession::on_frame(const Frame &f)
  {
    if (auto *l = std::get_if<from::Logon>(&f.msg))
    {
      if (login_done_)
        return; // a second Logon response: ignored
      login_done_ = true;
      session_id_ = int32_t(l->SessionID);
      logged_in_ = true;
      ev_.logged_in(int32_t(l->SessionID), uint32_t(f.seq));
      return;
    }
    if (auto *l = std::get_if<from::Logout>(&f.msg))
    {
      // Answer with our own Logout (UST OUCH 5.2, p.8), then close.
      to::Logout r{};
      r.set_UserID(user_);
      r.SessionID = login_done_ ? session_id_.load() : int32_t(l->SessionID);
      std::memcpy(r.Reason, l->Reason, sizeof(r.Reason));
      auto out = to_frame(next_out_++, ms_of_day(epoch_ms_now()), encoded(r));
      boost::system::error_code ec;
      asio::write(sock_, asio::buffer(out), ec);
      bool before = !login_done_;
      dead_ = true;
      close();
      ev_.logged_out(std::string(l->get_Reason()), before);
      return;
    }
    if (!login_done_)
      return fail(std::string("message type '") + f.type + "' before the Logon response");
    if (std::holds_alternative<from::Heartbeat>(f.msg))
    {
      // Answer each Heartbeat (UST OUCH 5.3, p.8).
      to::Heartbeat h{};
      h.SessionID = session_id_.load();
      send_now(encoded(h));
    }
    ev_.message(uint32_t(f.seq), f.ts, f.data, f.len);
  }

  SessionFactory tcp_session_factory()
  {
    return [](const SessionConfig &cfg, SessionEvents &ev) -> std::unique_ptr<CxSession> {
      return std::make_unique<TcpSession>(cfg, ev);
    };
  }
}
