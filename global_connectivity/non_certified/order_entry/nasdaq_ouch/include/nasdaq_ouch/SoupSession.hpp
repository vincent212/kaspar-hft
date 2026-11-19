/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// SoupSession: OuchSession over SoupBinTCP 3.0 on a TCP socket, with
// Boost.Asio on one thread of its own.
//
// It frames and parses SoupBinTCP packets with the code generated from
// global_connectivity/non_certified/market_data/nasdaq/soupbintcp.tbl and the OUCH requests with that from
// ouch50.tbl; it does not interpret OUCH reports, only numbers them and hands
// them on (SessionEvents::message).
//
// SoupBinTCP behaviour it implements (SoupBinTCP 3.0):
//   - Login Request first; Login Accepted gives the session and the sequence
//     number of the next Sequenced Data packet, and each one after counts
//     up by one (1.2, 2.2.1, 2.2.3)
//   - Login Rejected: login_rejected, then the server closes; no
//     disconnected event follows (2.2.2)
//   - a Client Heartbeat after heartbeat_ms without sending; the link is
//     declared down after receive_timeout_ms without receiving anything,
//     data or Server Heartbeat (1.3)
//   - End of Session: end_of_session, then the connection closes (2.2.5)
//   - Debug packets are ignored (2.1)
//
// Requests are queued for the socket; above max_queued_bytes a request
// returns Busy, and available() follows once the queue has drained to half
// that. Nothing is resent after a disconnect.
//
// Never run against a venue by this code's tests: they use a loopback
// server in the test process.

#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include "nasdaq_ouch/OuchSession.hpp"

namespace nasdaq::ouch
{
  class SoupSession : public OuchSession
  {
  public:
    SoupSession(const SessionConfig &cfg, SessionEvents &ev);
    ~SoupSession() override; // closes the socket and joins the thread: no events after

    void logon(const LogonParams &p) override;
    CallStatus enter(const to_nasdaq::EnterOrder &m, std::string_view appendage) override;
    CallStatus cancel(const to_nasdaq::CancelOrderRequest &m) override;
    CallStatus account_query() override;
    void logout() override;

  private:
    using Clock = std::chrono::steady_clock;

    CallStatus send_packet(std::string packet); // any thread
    CallStatus send_ouch(std::string_view ouch);

    // I/O thread only.
    void do_connect();
    void do_read();
    void on_data();
    void do_write();
    void queue_write(std::string packet, bool counted = false);
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
    std::deque<std::pair<std::string, bool>> wq_; // packet, counted in queued_
    bool writing_ = false;
    bool connected_ = false;
    bool dead_ = false;       // failed, rejected, ended or logged out: no more events
    bool login_done_ = false;
    uint64_t seq_ = 0;        // sequence number of the next Sequenced Data packet
    Clock::time_point started_, last_send_, last_recv_;

    // Any thread.
    std::atomic<bool> logged_in_{false};
    std::atomic<size_t> queued_{0};
    std::atomic<bool> busy_{false};

    std::thread thr_; // last: starts after the members it uses
  };
}
