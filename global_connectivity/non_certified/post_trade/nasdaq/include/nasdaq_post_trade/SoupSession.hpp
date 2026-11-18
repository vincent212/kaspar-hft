/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// SoupSession: a SoupBinTCP 3.00 client carrying DROP, over TCP.
//
// Not certified: it has been run only against SimDropServer on the loopback
// interface, never against Nasdaq.
//
// One thread per session. It connects, sends the Login Request, and then reads
// packets: Login Accepted, Sequenced Data (each one DROP line, numbered by
// counting from the sequence number in Login Accepted), Server Heartbeat, End
// of Session, Debug. It sends a Client Heartbeat after heartbeat_ms without
// sending anything, and treats server_timeout_ms without receiving anything as
// a lost connection. After a lost connection it waits reconnect_ms and logs in
// again to the same session at the next sequence number it has not delivered.
// A Login Rejected or an End of Session stops it for good.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

#include "nasdaq_post_trade/DropSession.hpp"

namespace nasdaq_post_trade
{
  struct SoupSessionConfig
  {
    std::string host;
    int port = 0;
    int heartbeat_ms = 1000;      // SoupBinTCP 1.3: at least once per second
    int server_timeout_ms = 15000; // silence that counts as a lost link
    int reconnect_ms = 2000;
    int connect_timeout_ms = 5000;
  };

  class SoupSession : public DropSession
  {
  public:
    explicit SoupSession(SoupSessionConfig c) : cfg_(std::move(c)) {}
    ~SoupSession() override { stop(); }

    void start(const LoginParams &p, DropSessionListener *l) override;
    void stop() override;

    // The packets the client sends, exposed for the encoding tests.
    static std::string encode_login(const LoginParams &p);
    static std::string encode_heartbeat();
    static std::string encode_logout();

  private:
    enum class End
    {
      Lost,  // reconnect
      Final, // login rejected, end of session, protocol error: stop
      Stop,  // stop() was called
    };

    void run(std::stop_token st);
    End connection(std::stop_token st, int fd);
    int connect_socket(std::string &err) const;
    bool send_all(int fd, const std::string &bytes);

    SoupSessionConfig cfg_;
    LoginParams login_;
    DropSessionListener *listener_ = nullptr;

    // Owned by the session thread once started.
    std::string session_;    // from Login Accepted
    uint64_t next_seq_ = 0;  // sequence number of the next Sequenced Data packet

    std::mutex mu_;
    std::condition_variable_any cv_;
    std::jthread thread_;
  };
}
