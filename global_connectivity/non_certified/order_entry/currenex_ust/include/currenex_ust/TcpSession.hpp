/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// TcpSession: CxSession over a TCP socket, with Boost.Asio on one thread of
// its own.
//
// Behaviour (Currenex UST OUCH 2.0):
//   - Logon first, with sequence number 1; every later message the client
//     sends takes the next number (5.1.1, p.8); the header timestamp is
//     milliseconds past midnight UTC (4.2.4, p.7)
//   - Logon response: logged_in with the SessionID and the response's
//     sequence number; every later sequenced message is passed on with its
//     own number (the receiver checks them)
//   - each Currenex Heartbeat is answered with a Heartbeat (5.3, p.8) and
//     also passed on (it is numbered)
//   - a Currenex Logout is answered with a Logout (5.2, p.8), then
//     logged_out and the connection closes
//   - nothing received for receive_timeout_ms (Currenex heartbeats every 3
//     s): the link is down
//
// Requests are queued for the socket; above max_queued_bytes a request
// returns Busy, and available() follows once the queue has drained to half
// that. Nothing is resent after a disconnect.
//
// Never run against Currenex by this code's tests: they use a loopback
// server in the test process.

#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include "currenex_ust/CxSession.hpp"

namespace currenex::ouch
{
  class TcpSession : public CxSession
  {
  public:
    TcpSession(const SessionConfig &cfg, SessionEvents &ev);
    ~TcpSession() override; // closes the socket and joins the thread: no events after

    void logon(const LogonParams &p) override;
    CallStatus new_order(const to::NewOrder &m) override;
    CallStatus cancel(const to::OrderCancelRequest &m) override;
    CallStatus instrument_info_request() override;
    CallStatus routing_user_info_request() override;
    CallStatus resend(uint32_t begin) override;
    void logout() override;

  private:
    using Clock = std::chrono::steady_clock;

    // Any thread: the message is framed on the I/O thread, where the
    // sequence number is assigned in send order.
    CallStatus send_request(std::string msg, bool needs_session_id);

    // I/O thread only.
    void do_connect();
    void do_read();
    void on_data();
    void on_frame(const Frame &f);
    void send_now(std::string msg, bool counted = false);
    void do_write();
    void tick();
    void fail(const std::string &why);
    void close();

    SessionConfig cfg_;
    SessionEvents &ev_;
    boost::asio::io_context io_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket sock_;
    boost::asio::steady_timer timer_;

    // I/O thread only.
    LogonParams logon_;
    std::vector<char> rbuf_;
    size_t rlen_ = 0;
    std::deque<std::pair<std::string, size_t>> wq_; // frame, bytes counted in queued_
    bool writing_ = false;
    bool connected_ = false;
    bool dead_ = false;      // failed, logged out or closed: no more events
    bool login_done_ = false;
    int32_t next_out_ = 1;   // sequence number of the next message sent
    std::string user_;       // Logout carries the UserID
    Clock::time_point started_, connected_at_, last_recv_;

    // Any thread.
    std::atomic<bool> logged_in_{false};
    std::atomic<int32_t> session_id_{0};
    std::atomic<size_t> queued_{0};
    std::atomic<bool> busy_{false};

    std::thread thr_; // last: starts after the members it uses
  };
}
