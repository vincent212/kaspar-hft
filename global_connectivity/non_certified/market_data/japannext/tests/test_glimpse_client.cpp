/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The GLIMPSE client actor against a fake SoupBinTCP server on the loopback:
// what it sends the feed when the server rejects the login, ends the session,
// closes the connection, or completes the snapshot.

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <string>
#include <thread>

#include "TestUtil.hpp"
#include "jnx_md/act/GlimpseClient.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace jnx_md;

namespace
{
  // Accepts one connection, reads the Login Request, sends `reply`, with
  // wait_for_logout reads what the client sends until a Logout Request or
  // the client closes, and closes. finish() waits for all of that.
  class FakeServer
  {
  public:
    explicit FakeServer(std::string reply, bool wait_for_logout = false)
        : reply_(std::move(reply)), wait_for_logout_(wait_for_logout)
    {
      fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
      sockaddr_in a{};
      a.sin_family = AF_INET;
      a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      ::bind(fd_, reinterpret_cast<sockaddr *>(&a), sizeof(a));
      socklen_t n = sizeof(a);
      ::getsockname(fd_, reinterpret_cast<sockaddr *>(&a), &n);
      port_ = ntohs(a.sin_port);
      ::listen(fd_, 1);
      th_ = std::thread([this] {
        const int c = ::accept(fd_, nullptr, nullptr);
        if (c < 0)
          return;
        char b[256];
        std::string got;
        while (got.size() < 49)   // the Login Request
        {
          const ssize_t r = ::recv(c, b, sizeof(b), 0);
          if (r <= 0)
            break;
          got.append(b, size_t(r));
        }
        login_ = got.size() >= 49 && got[2] == 'L';
        (void)::send(c, reply_.data(), reply_.size(), 0);
        if (wait_for_logout_)
        {
          timeval tv{2, 0};
          ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
          for (;;)
          {
            const ssize_t r = ::recv(c, b, sizeof(b), 0);
            if (r <= 0)
              break;
            got.append(b, size_t(r));
            if (got.find('O', 49) != std::string::npos)
              break;
          }
        }
        logout_ = got.find("\0\1O", 49, 3) != std::string::npos;
        ::close(c);
      });
    }
    ~FakeServer()
    {
      finish();
      ::close(fd_);
    }
    void finish()
    {
      if (th_.joinable())
        th_.join();
    }
    uint16_t port() const { return port_; }
    bool login() const { return login_; }
    bool logout() const { return logout_; }

  private:
    std::string reply_;
    bool wait_for_logout_;
    int fd_;
    uint16_t port_;
    std::thread th_;
    std::atomic<bool> login_{false}, logout_{false};
  };

  struct Session
  {
    unit_test::MockActor feed{"feed"};
    size_t data = 0, failed = 0;
    std::string bytes, why;
    explicit Session(uint16_t port)
    {
      act::GlimpseConfig g;
      g.host = "127.0.0.1";
      g.port = port;
      g.username = "user";
      g.password = "pw";
      act::GlimpseClient client(g, &feed);
      msg::SnapshotRequest req;
      unit_test::TestHelper::invoke_handler(&client, &req, nullptr);
      for (size_t i = 0; i < feed.message_count(); ++i)
      {
        if (auto *d = feed.get_message<msg::GlimpseData>(i))
        {
          ++data;
          bytes += d->bytes;
        }
        else if (auto *f = feed.get_message<msg::GlimpseFailed>(i))
        {
          ++failed;
          why = f->why;
        }
      }
    }
  };
}

// The rejection reaches the feed in the bytes; the client does not report it
// again as a failure.
TEST(GlimpseClient, LoginRejectedIsLeftToTheFeed)
{
  const std::string rej = soup_packet('J', "A");
  FakeServer server(rej);
  Session r(server.port());
  server.finish();
  EXPECT_TRUE(server.login());
  EXPECT_EQ(r.bytes, rej);
  EXPECT_EQ(r.failed, 0u);
}

TEST(GlimpseClient, EndOfSessionIsLeftToTheFeed)
{
  const std::string z = soup_packet('Z', "");
  FakeServer server(soup_packet('A', "          00000000000000000001") + z);   // Login Accepted
  Session r(server.port());
  server.finish();
  EXPECT_TRUE(server.login());
  EXPECT_NE(r.bytes.find(z), std::string::npos);
  EXPECT_EQ(r.failed, 0u);
}

// A connection closed without a word is the client's to report.
TEST(GlimpseClient, AClosedConnectionIsReported)
{
  FakeServer server("");
  Session r(server.port());
  server.finish();
  EXPECT_TRUE(server.login());
  EXPECT_EQ(r.failed, 1u);
  EXPECT_NE(r.why.find("closed"), std::string::npos);
}

TEST(GlimpseClient, EndOfSnapshotEndsTheSessionWithALogout)
{
  jnx::glimpse::from_jnx::EndOfSnapshotMessage g{};
  g.SequenceNumber = 42;
  const std::string snap = soup_packet('S', bytes_of(g));
  FakeServer server(snap, true);
  Session r(server.port());
  server.finish();
  EXPECT_EQ(r.bytes, snap);
  EXPECT_EQ(r.failed, 0u);
  EXPECT_TRUE(server.logout());
}
