/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// TcpSession, the real Currenex session, against a server in this test
// process on 127.0.0.1. Nothing here connects anywhere else.
//
// The server side reads and writes frames byte by byte from the
// specification (SOH, 4-byte sequence number, 4-byte timestamp, type,
// body, ETX; big-endian), independently of the generated code the session
// uses. Body lengths per type are the spec's totals less the 9 header bytes.

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/time.h>

#include <map>
#include <mutex>

#include <boost/asio.hpp>

#include "TestUtil.hpp"
#include "currenex_ust/CxCodec.hpp"
#include "currenex_ust/TcpSession.hpp"

namespace asio = boost::asio;
using asio::ip::tcp;
using namespace currenex::ouch;
using namespace currenex::ouch::test;

namespace
{
  std::string be(uint64_t v, int n)
  {
    std::string s;
    for (int i = n - 1; i >= 0; --i)
      s.push_back(char((v >> (8 * i)) & 0xff));
    return s;
  }
  std::string alpha(std::string_view s, size_t n)
  {
    std::string r(s.substr(0, n));
    r.resize(n, ' ');
    return r;
  }
  uint32_t get_be(const std::string &s, size_t off, int n)
  {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i)
      v = v << 8 | uint8_t(s[off + i]);
    return v;
  }

  // Client message totals, from offset 0 (UST OUCH 10).
  const std::map<char, size_t> CLIENT_SIZE = {{'a', 55}, {'B', 36}, {'C', 13}, {'E', 13}, {'G', 13},
                                              {'L', 54}, {'N', 23}, {'P', 39}, {'S', 39}, {'2', 13}};

  std::string server_frame(uint32_t seq, char type, const std::string &body)
  {
    return std::string(1, '\x01') + be(seq, 4) + be(1000, 4) + std::string(1, type) + body + std::string(1, '\x03');
  }
  std::string logon_response(uint32_t seq, int32_t session)
  {
    return server_frame(seq, 'a', alpha("AbcUser", 20) + alpha("", 20) + be(uint32_t(session), 4) + be(200, 2));
  }
  std::string heartbeat(uint32_t seq, int32_t session) { return server_frame(seq, 'C', be(uint32_t(session), 4)); }
  std::string logout(uint32_t seq, const std::string &reason)
  {
    return server_frame(seq, 'B', alpha("AbcUser", 20) + be(1307, 4) + alpha(reason, 3));
  }
  std::string cancelled(uint32_t seq, int32_t clid)
  {
    return server_frame(seq, 'R', be(uint32_t(clid), 4) + be(77, 8) + "C" + be(0, 2) + be(0xffffffff, 4));
  }

  // The server: one connection on 127.0.0.1, blocking calls with a timeout.
  struct Loopback
  {
    asio::io_context io;
    tcp::acceptor acc{io, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)};
    tcp::socket s{io};

    std::string port() const { return std::to_string(acc.local_endpoint().port()); }
    void accept()
    {
      acc.accept(s);
      timeval tv{3, 0};
      setsockopt(s.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    // The next client message, from the header's first byte to the end of
    // its body (SOH and ETX checked and removed).
    std::string read()
    {
      char soh;
      asio::read(s, asio::buffer(&soh, 1));
      EXPECT_EQ(soh, '\x01');
      std::string h(9, '\0');
      asio::read(s, asio::buffer(h));
      auto it = CLIENT_SIZE.find(h[8]);
      EXPECT_NE(it, CLIENT_SIZE.end()) << "type " << h[8];
      std::string b(it == CLIENT_SIZE.end() ? 0 : it->second - 9, '\0');
      asio::read(s, asio::buffer(b));
      char etx;
      asio::read(s, asio::buffer(&etx, 1));
      EXPECT_EQ(etx, '\x03');
      return h + b;
    }
    void write(const std::string &bytes) { asio::write(s, asio::buffer(bytes)); }
    void close()
    {
      boost::system::error_code ec;
      s.close(ec);
    }
  };

  struct Events : SessionEvents
  {
    std::mutex mu;
    std::vector<std::string> log;
    std::vector<std::pair<uint32_t, std::string>> msgs;
    int32_t session = 0;
    uint32_t seq = 0;

    void add(std::string e)
    {
      std::lock_guard<std::mutex> l(mu);
      log.push_back(std::move(e));
    }
    void logged_in(int32_t s, uint32_t n) override
    {
      {
        std::lock_guard<std::mutex> l(mu);
        session = s;
        seq = n;
      }
      add("logged_in");
    }
    void logged_out(const std::string &r, bool before) override
    {
      add("logged_out " + r + (before ? " before" : ""));
    }
    void disconnected(const std::string &) override { add("disconnected"); }
    void busy() override { add("busy"); }
    void available() override { add("available"); }
    void message(uint32_t s, int32_t, const char *d, size_t n) override
    {
      std::lock_guard<std::mutex> l(mu);
      msgs.push_back({s, std::string(d, n)});
    }
    void undecoded(uint32_t s, char t) override { add("undecoded " + std::to_string(s) + " " + t); }
    bool has(const std::string &e)
    {
      std::lock_guard<std::mutex> l(mu);
      return std::find(log.begin(), log.end(), e) != log.end();
    }
    size_t nmsgs()
    {
      std::lock_guard<std::mutex> l(mu);
      return msgs.size();
    }
  };

  SessionConfig config(const Loopback &srv)
  {
    SessionConfig c;
    c.host = "127.0.0.1";
    c.port = srv.port();
    return c;
  }

  LogonParams logon() { return {"AbcUser", "123pswd", 200}; }
}

// Logon is sequence number 1 (UST OUCH 5.1.1); every later client message
// takes the next number; requests carry the SessionID from the response.
TEST(TcpSession, LogonAndRequestsAreFramedAndNumbered)
{
  Loopback srv;
  Events ev;
  TcpSession s(config(srv), ev);
  s.logon(logon());
  srv.accept();
  auto l = srv.read();
  ASSERT_EQ(l.size(), 55u);
  EXPECT_EQ(get_be(l, 0, 4), 1u);
  EXPECT_LT(get_be(l, 4, 4), 86'400'000u); // ms past midnight
  EXPECT_EQ(l[8], 'a');
  EXPECT_EQ(l.substr(9, 20), alpha("AbcUser", 20));
  EXPECT_EQ(l.substr(29, 20), alpha("123pswd", 20));
  EXPECT_EQ(get_be(l, 49, 4), 0u);
  EXPECT_EQ(get_be(l, 53, 2), 200u);

  srv.write(logon_response(8, 1307));
  ASSERT_TRUE(wait_until([&] { return ev.has("logged_in"); }));
  EXPECT_EQ(ev.session, 1307);
  EXPECT_EQ(ev.seq, 8u);

  ASSERT_EQ(s.instrument_info_request(), CallStatus::Success);
  auto e = srv.read();
  EXPECT_EQ(get_be(e, 0, 4), 2u);
  EXPECT_EQ(e[8], 'E');
  EXPECT_EQ(get_be(e, 9, 4), 1307u);

  ASSERT_EQ(s.routing_user_info_request(), CallStatus::Success);
  auto g = srv.read();
  EXPECT_EQ(get_be(g, 0, 4), 3u);
  EXPECT_EQ(g[8], 'G');
  EXPECT_EQ(get_be(g, 9, 4), 1307u);

  to::NewOrder o{};
  o.ClOrderID = 465889775;
  o.OrderType = 'F';
  o.InstrumentIndex = 5;
  o.Side = 'B';
  o.OrderAmt = 0x3d0b27;
  o.MinAmt = 0;
  o.Price = 0x170CF9EB65;
  o.ShowAmt = 0;
  o.ExpireType = 'G';
  o.RoutedUserKey = -1;
  ASSERT_EQ(s.new_order(o), CallStatus::Success);
  auto n = srv.read();
  EXPECT_EQ(get_be(n, 0, 4), 4u);
  EXPECT_EQ(n.substr(8), encoded(o));

  ASSERT_EQ(s.resend(1), CallStatus::Success);
  auto r = srv.read();
  EXPECT_EQ(get_be(r, 0, 4), 5u);
  EXPECT_EQ(r[8], '2');
  EXPECT_EQ(get_be(r, 9, 4), 1u);
}

// Each Currenex Heartbeat is answered (UST OUCH 5.3) and passed on, since
// it is numbered.
TEST(TcpSession, HeartbeatIsAnsweredAndPassedOn)
{
  Loopback srv;
  Events ev;
  TcpSession s(config(srv), ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  srv.write(logon_response(1, 1307) + heartbeat(2, 1307));
  auto h = srv.read();
  EXPECT_EQ(h[8], 'C');
  EXPECT_EQ(get_be(h, 0, 4), 2u);
  EXPECT_EQ(get_be(h, 9, 4), 1307u);
  ASSERT_TRUE(wait_until([&] { return ev.nmsgs() == 1; }));
  EXPECT_EQ(ev.msgs[0].first, 2u);
  EXPECT_EQ(ev.msgs[0].second[0], 'C');
}

// A frame split across reads, byte by byte, is still one frame; frames
// arrive back to back in one block.
TEST(TcpSession, FramesSplitAcrossReadsAndBatched)
{
  Loopback srv;
  Events ev;
  TcpSession s(config(srv), ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  std::string all = logon_response(1, 1307) + cancelled(2, 77) + cancelled(3, 78);
  for (char c : all)
  {
    srv.write(std::string(1, c));
    std::this_thread::sleep_for(500us);
  }
  ASSERT_TRUE(wait_until([&] { return ev.nmsgs() == 2; }));
  EXPECT_EQ(ev.msgs[0].first, 2u);
  EXPECT_EQ(ev.msgs[1].first, 3u);
  EXPECT_EQ(ev.msgs[1].second[0], 'R');
  EXPECT_EQ(get_be(ev.msgs[1].second, 1, 4), 78u);
}

// An unlisted type is reported with its number and skipped; bytes outside
// a frame are skipped.
TEST(TcpSession, UnknownTypeAndGarbage)
{
  Loopback srv;
  Events ev;
  TcpSession s(config(srv), ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  srv.write(logon_response(1, 1307) + "junk" + server_frame(2, 'z', "abcdef") + cancelled(3, 9));
  ASSERT_TRUE(wait_until([&] { return ev.nmsgs() == 1; }));
  EXPECT_TRUE(ev.has("undecoded 2 z"));
  EXPECT_EQ(ev.msgs[0].first, 3u);
}

// A Logout from Currenex is answered with a Logout (UST OUCH 5.2).
TEST(TcpSession, CurrenexLogoutIsAnswered)
{
  Loopback srv;
  Events ev;
  TcpSession s(config(srv), ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  srv.write(logon_response(1, 1307) + logout(2, "A9"));
  auto b = srv.read();
  EXPECT_EQ(b[8], 'B');
  EXPECT_EQ(b.substr(9, 20), alpha("AbcUser", 20));
  EXPECT_EQ(get_be(b, 29, 4), 1307u);
  EXPECT_EQ(b.substr(33, 3), "A9 ");
  ASSERT_TRUE(wait_until([&] { return ev.has("logged_out A9"); }));
  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(ev.has("disconnected"));
}

// A Logout in place of the Logon response: the logon was refused.
TEST(TcpSession, LogonRefused)
{
  Loopback srv;
  Events ev;
  TcpSession s(config(srv), ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  srv.write(logout(1, "A5"));
  ASSERT_TRUE(wait_until([&] { return ev.has("logged_out A5 before"); }));
  EXPECT_FALSE(ev.has("logged_in"));
}

TEST(TcpSession, RequestsBeforeLogonAreRefused)
{
  Loopback srv;
  Events ev;
  TcpSession s(config(srv), ev);
  EXPECT_EQ(s.instrument_info_request(), CallStatus::Disconnected);
  EXPECT_EQ(s.new_order(to::NewOrder{}), CallStatus::Disconnected);
}

// Nothing received (no heartbeats): the link is down.
TEST(TcpSession, SilenceIsADisconnect)
{
  Loopback srv;
  Events ev;
  auto c = config(srv);
  c.receive_timeout_ms = 300;
  TcpSession s(c, ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  srv.write(logon_response(1, 1307));
  ASSERT_TRUE(wait_until([&] { return ev.has("disconnected"); }, std::chrono::seconds(3)));
}

TEST(TcpSession, NoLogonResponseIsADisconnect)
{
  Loopback srv;
  Events ev;
  auto c = config(srv);
  c.logon_timeout_ms = 300;
  TcpSession s(c, ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  ASSERT_TRUE(wait_until([&] { return ev.has("disconnected"); }, std::chrono::seconds(3)));
}

TEST(TcpSession, ServerCloseIsADisconnect)
{
  Loopback srv;
  Events ev;
  TcpSession s(config(srv), ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  srv.write(logon_response(1, 1307));
  ASSERT_TRUE(wait_until([&] { return ev.has("logged_in"); }));
  srv.close();
  ASSERT_TRUE(wait_until([&] { return ev.has("disconnected"); }));
  EXPECT_EQ(s.new_order(to::NewOrder{}), CallStatus::Disconnected);
}

// The client's own Logout carries reason A6 (User Logout); no events after.
TEST(TcpSession, ClientLogout)
{
  Loopback srv;
  Events ev;
  TcpSession s(config(srv), ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  srv.write(logon_response(1, 1307));
  ASSERT_TRUE(wait_until([&] { return ev.has("logged_in"); }));
  s.logout();
  auto b = srv.read();
  EXPECT_EQ(b[8], 'B');
  EXPECT_EQ(get_be(b, 0, 4), 2u);
  EXPECT_EQ(b.substr(33, 3), "A6 ");
  std::this_thread::sleep_for(50ms);
  EXPECT_FALSE(ev.has("disconnected"));
}
