/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// SimDropServer: an in-process DROP host speaking SoupBinTCP 3.00, listening
// on the loopback interface only. It lets SoupSession and NasdaqDropClient run
// end to end without Nasdaq: for --sim runs, demos and tests.
//
// What it models:
//   - one SoupBinTCP session (an id and a journal of DROP lines numbered from 1)
//     and one client connection at a time;
//   - login: username and password (case-insensitive), the requested session
//     (blank or this one, else Login Rejected 'S'), the requested sequence
//     number (1 replays the whole session, n resumes at n, 0 starts at the most
//     recent line, a number past the end starts at the end);
//   - Sequenced Data in order, Server Heartbeats after heartbeat_ms of silence,
//     End of Session;
//   - faults: a dropped connection, an end of session;
//   - a trading generator (on a frame::som::TimedRunner) that writes accepted
//     orders, executions, an occasional break and an occasional correction.
// What it does not model: Nasdaq's real session ids, its timing, its order
// handling, and anything the specification does not describe (feed.md,
// section 10).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "nasdaq_post_trade/DropLine.hpp"

namespace frame::som
{
  class TimedRunner;
}

namespace nasdaq_post_trade
{
  struct SimDropConfig
  {
    std::string username = "SIMUSR";
    std::string password = "SIMPASS";
    std::string session = "SIM0000001";
    int port = 0; // 0: any free port; see port()
    int heartbeat_ms = 1000;
  };

  class SimDropServer
  {
  public:
    explicit SimDropServer(SimDropConfig c);
    ~SimDropServer();

    // Listen on 127.0.0.1. False with the reason in *err on failure.
    bool start(std::string *err = nullptr);
    int port() const { return port_; }
    const SimDropConfig &config() const { return cfg_; }

    // Append a line (LINE_SIZE bytes, with or without CR/LF) to the session.
    void publish(const std::string &line);
    void publish(const DropReport &r) { publish(format_line(r)); }

    // Faults, carried out by the server thread.
    void drop_connection();
    void end_session();

    // Generate trading: every `every`, an accepted order and its execution; a
    // break of an earlier execution after every 7th, a correction after every
    // 11th.
    void start_trading(std::chrono::milliseconds every, std::vector<std::string> symbols);

    struct Login
    {
      std::string username, session;
      uint64_t requested_seq = 0;
      uint64_t accepted_seq = 0; // 0 when rejected
    };
    std::vector<Login> logins() const;
    size_t client_heartbeats() const { return heartbeats_; }
    size_t logouts() const { return logouts_; }
    size_t connections() const { return connections_; }
    size_t journal_size() const;
    std::string line(uint64_t seq) const; // 1-based
    bool client_logged_in() const { return logged_in_; }

  private:
    void run(std::stop_token st);
    void close_client();
    bool send_packet(const std::string &p);
    void on_client_bytes();
    void generate(frame::som::TimedRunner *r);

    SimDropConfig cfg_;
    int listen_fd_ = -1;
    int port_ = 0;

    mutable std::mutex mu_;          // journal_, logins_
    std::vector<std::string> journal_;
    std::vector<Login> logins_;
    std::atomic<bool> drop_{false}, end_{false};
    std::atomic<bool> ended_{false};
    std::atomic<size_t> heartbeats_{0}, logouts_{0}, connections_{0};
    std::atomic<bool> logged_in_{false};

    // server thread only
    int client_fd_ = -1;
    std::string inbuf_;
    uint64_t cursor_ = 0; // next sequence number to send
    std::chrono::steady_clock::time_point last_sent_;

    // generator
    std::mutex gen_mu_;
    std::vector<std::string> symbols_;
    std::chrono::milliseconds every_{0};
    uint64_t next_ref_ = 1000, next_match_ = 5000, n_exec_ = 0;
    std::vector<DropReport> executions_;

    std::jthread thread_;
    std::unique_ptr<frame::som::TimedRunner> runner_; // destroyed first
  };
}
