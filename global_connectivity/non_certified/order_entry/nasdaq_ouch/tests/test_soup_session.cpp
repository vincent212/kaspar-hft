/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SoupSession, the real SoupBinTCP session, against a server in this test
// process on 127.0.0.1. Nothing here connects anywhere else.
//
// The server side is written from the SoupBinTCP 3.0 tables byte by byte
// (2-byte big-endian length counting the type and payload, then the type),
// independently of the generated code the session uses.

#include <gtest/gtest.h>

#include <sys/socket.h>
#include <sys/time.h>

#include <mutex>

#include "actors/act/Manager.hpp"
#include "frame/som/msg/Order.hpp"

#include "TestUtil.hpp"
#include "nasdaq_ouch/OuchCodec.hpp"
#include "nasdaq_ouch/SoupSession.hpp"
#include "nasdaq_ouch/act/OuchOM.hpp"

namespace asio = boost::asio;
using asio::ip::tcp;
using namespace nasdaq::ouch;
using namespace nasdaq::ouch::test;
namespace from = nasdaq::ouch::from_nasdaq;

namespace
{
  // One SoupBinTCP packet: length, type, payload (SoupBinTCP 3.0 1.1).
  std::string packet(char type, std::string_view payload = {})
  {
    size_t n = payload.size() + 1;
    std::string p;
    p.push_back(char(n >> 8));
    p.push_back(char(n & 0xff));
    p.push_back(type);
    p.append(payload);
    return p;
  }

  std::string login_accepted(std::string_view session, uint64_t seq)
  {
    std::string s(10 - session.size(), ' ');
    s.append(session); // left padded (2.2.1)
    auto n = std::to_string(seq);
    s.append(20 - n.size(), ' ');
    s.append(n);
    return packet('A', s);
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
    // Type and payload of the next packet from the client.
    std::string read()
    {
      unsigned char h[2];
      asio::read(s, asio::buffer(h, 2));
      std::string b(size_t(h[0]) << 8 | h[1], '\0');
      asio::read(s, asio::buffer(b));
      return b;
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
    std::vector<std::string> log; // event names
    std::vector<std::pair<uint64_t, std::string>> msgs;
    std::string session;
    uint64_t next = 0;

    void add(std::string e)
    {
      std::lock_guard<std::mutex> l(mu);
      log.push_back(std::move(e));
    }
    void logged_in(const std::string &s, uint64_t n) override
    {
      {
        std::lock_guard<std::mutex> l(mu);
        session = s;
        next = n;
      }
      add("logged_in");
    }
    void login_rejected(char r) override { add(std::string("login_rejected ") + r); }
    void disconnected(const std::string &) override { add("disconnected"); }
    void end_of_session() override { add("end_of_session"); }
    void busy() override { add("busy"); }
    void available() override { add("available"); }
    void message(uint64_t seq, const char *d, size_t n) override
    {
      std::lock_guard<std::mutex> l(mu);
      msgs.push_back({seq, std::string(d, n)});
    }
    bool has(const std::string &e)
    {
      std::lock_guard<std::mutex> l(mu);
      return std::find(log.begin(), log.end(), e) != log.end();
    }
    std::vector<std::string> events()
    {
      std::lock_guard<std::mutex> l(mu);
      return log;
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

  LogonParams logon(uint64_t seq = 1, std::string session = "")
  {
    return {"USER01", "pw", std::move(session), seq};
  }

  std::string system_event(char code)
  {
    from::SystemEvent e{};
    e.Timestamp = 1;
    e.EventCode = code;
    return to_bytes(e);
  }
}

TEST(SoupSession, LoginRequestLayoutAndSequencedData)
{
  Loopback srv;
  Events ev;
  SoupSession s(config(srv), ev);
  s.logon(logon(5, "SESS1"));
  srv.accept();
  auto l = srv.read();
  // Login Request (SoupBinTCP 3.0 2.3.1): type, username 6, password 10,
  // requested session 10, requested sequence number 20.
  ASSERT_EQ(l.size(), 47u);
  EXPECT_EQ(l.substr(0, 1), "L");
  EXPECT_EQ(l.substr(1, 6), "USER01");
  EXPECT_EQ(l.substr(7, 10), "pw        ");
  EXPECT_EQ(l.substr(17, 10), "     SESS1");
  EXPECT_EQ(l.substr(27, 20), std::string(19, ' ') + "5");

  srv.write(packet('+', "hello") + login_accepted("SESS1", 5) + packet('S', system_event('S')) + packet('H') +
            packet('S', system_event('E')));
  ASSERT_TRUE(wait_until([&] { return ev.nmsgs() == 2; }));
  EXPECT_EQ(ev.session, "SESS1");
  EXPECT_EQ(ev.next, 5u);
  EXPECT_EQ(ev.msgs[0].first, 5u);
  EXPECT_EQ(ev.msgs[0].second, system_event('S'));
  EXPECT_EQ(ev.msgs[1].first, 6u);
  EXPECT_EQ(ev.msgs[1].second, system_event('E'));
}

// A packet split across reads, byte by byte, is still one packet.
TEST(SoupSession, PacketsSplitAcrossReads)
{
  Loopback srv;
  Events ev;
  SoupSession s(config(srv), ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  std::string all = login_accepted("S", 1) + packet('S', system_event('S'));
  for (char c : all)
  {
    srv.write(std::string(1, c));
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_TRUE(wait_until([&] { return ev.nmsgs() == 1; }));
  EXPECT_EQ(ev.msgs[0].second, system_event('S'));
}

TEST(SoupSession, RequestsAreUnsequencedDataPackets)
{
  Loopback srv;
  Events ev;
  SoupSession s(config(srv), ev);
  to_nasdaq::CancelOrderRequest c{};
  c.UserRefNum = 9;
  EXPECT_EQ(s.cancel(c), CallStatus::Disconnected); // not logged in yet
  s.logon(logon());
  srv.accept();
  srv.read();
  srv.write(login_accepted("S", 1));
  ASSERT_TRUE(wait_until([&] { return ev.has("logged_in"); }));

  to_nasdaq::EnterOrder o{};
  o.UserRefNum = 7;
  o.Side = 'B';
  o.Quantity = 100;
  o.set_Symbol("MSFT");
  o.Price = 41'2500;
  o.TimeInForce = '0';
  o.Display = 'Y';
  o.Capacity = 'A';
  o.InterMarketSweepEligibility = 'N';
  o.CrossType = 'N';
  o.set_ClOrdID("1");
  std::string app = Appendage().alpha(TAG_FIRM, "M2TT", 4).str();
  ASSERT_EQ(s.enter(o, app), CallStatus::Success);
  auto p = srv.read();
  ASSERT_EQ(p.size(), 1u + 47u + 6u);
  EXPECT_EQ(p[0], 'U');
  EXPECT_EQ(p[1], 'O');
  EXPECT_EQ(p.substr(1 + 1, 4), std::string("\0\0\0\x07", 4));
  EXPECT_EQ(p.substr(1 + 45, 2), std::string("\0\x06", 2)); // Appendage Length
  EXPECT_EQ(p.substr(1 + 47), app);

  ASSERT_EQ(s.cancel(c), CallStatus::Success);
  EXPECT_EQ(srv.read(), std::string("UX\0\0\0\x09\0\0\0\0\0\0", 12));
  ASSERT_EQ(s.account_query(), CallStatus::Success);
  EXPECT_EQ(srv.read(), std::string("UQ\0\0", 4));
}

TEST(SoupSession, ClientHeartbeatWhenIdle)
{
  Loopback srv;
  Events ev;
  auto c = config(srv);
  c.heartbeat_ms = 100;
  SoupSession s(c, ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  srv.write(login_accepted("S", 1));
  EXPECT_EQ(srv.read(), "R"); // Client Heartbeat (2.3.3)
}

TEST(SoupSession, SilenceIsADisconnect)
{
  Loopback srv;
  Events ev;
  auto c = config(srv);
  c.receive_timeout_ms = 300;
  SoupSession s(c, ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  srv.write(login_accepted("S", 1));
  EXPECT_TRUE(wait_until([&] { return ev.has("disconnected"); }, 2s));
}

TEST(SoupSession, LoginRejectedIsNotADisconnect)
{
  Loopback srv;
  Events ev;
  SoupSession s(config(srv), ev);
  s.logon(logon());
  srv.accept();
  srv.read();
  srv.write(packet('J', "A"));
  srv.close();
  ASSERT_TRUE(wait_until([&] { return ev.has("login_rejected A"); }));
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(ev.events(), (std::vector<std::string>{"login_rejected A"}));
}

TEST(SoupSession, ServerCloseAndEndOfSession)
{
  {
    Loopback srv;
    Events ev;
    SoupSession s(config(srv), ev);
    s.logon(logon());
    srv.accept();
    srv.read();
    srv.write(login_accepted("S", 1));
    srv.close();
    EXPECT_TRUE(wait_until([&] { return ev.has("disconnected"); }));
  }
  {
    Loopback srv;
    Events ev;
    SoupSession s(config(srv), ev);
    s.logon(logon());
    srv.accept();
    srv.read();
    srv.write(login_accepted("S", 1) + packet('Z'));
    ASSERT_TRUE(wait_until([&] { return ev.has("end_of_session"); }));
    srv.close();
    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(ev.has("disconnected"));
  }
}

TEST(SoupSession, NothingListeningIsADisconnect)
{
  std::string port;
  {
    Loopback srv;
    port = srv.port();
  } // closed: nothing listens there now
  Events ev;
  SessionConfig c;
  c.host = "127.0.0.1";
  c.port = port;
  SoupSession s(c, ev);
  s.logon(logon());
  EXPECT_TRUE(wait_until([&] { return ev.has("disconnected"); }));
}

// OuchOM over the real session and the loopback server: login, Account
// Query, an order, its acceptance.
TEST(SoupSession, OuchOMEndToEndOverLoopback)
{
  Loopback srv;
  Recorder somr("SOM");
  auto msft = test_stock("MSFT");
  OuchOMConfig cfg;
  cfg.session.host = "127.0.0.1";
  cfg.session.port = srv.port();
  cfg.username = "USER01";
  cfg.password = "pw";
  auto om = static_cast<OuchOM *>(create_OuchOM(cfg, &somr));
  ASSERT_NE(om, nullptr);
  auto mgr = std::make_unique<actors::Manager>("loopback");
  mgr->manage_and_start(om);

  srv.accept();
  auto l = srv.read();
  EXPECT_EQ(l.substr(27, 20), std::string(19, ' ') + "0"); // first login: sequence 0
  srv.write(login_accepted("DAY1", 1));
  EXPECT_EQ(srv.read(), std::string("UQ\0\0", 4)); // Account Query
  from::AccountQueryResponse q{};
  q.NextUserRefNum = 10;
  srv.write(packet('S', to_bytes(q)));
  ASSERT_TRUE(wait_until([&] { return om->ready(); }));

  frame::som::msg::Order o("EQ", en::x::NASDAQ, msft->id, 100, 4125, en::bs::BUY, 4);
  o.oid = 1;
  om->send(new frame::som::msg::Order(o), &somr);
  auto p = srv.read();
  ASSERT_GE(p.size(), 48u);
  ASSERT_EQ(p.substr(0, 2), "UO");
  auto d = to_nasdaq::EnterOrder{};
  std::memcpy(&d, p.data() + 1, sizeof(d));
  EXPECT_EQ(uint32_t(d.UserRefNum), 10u);
  EXPECT_EQ(uint64_t(d.Price), 41'2500u);

  from::OrderAccepted a{};
  a.UserRefNum = 10;
  a.Quantity = 100;
  a.OrderReferenceNumber = 4242;
  a.OrderState = 'L';
  srv.write(packet('S', to_bytes(a)));
  auto ack = somr.wait_for<frame::som::msg::Ack>();
  ASSERT_NE(ack, nullptr);
  EXPECT_EQ(ack->xordid, 4242u);
  EXPECT_EQ(om->next_sequence(), 3u);

  mgr->end();
  EXPECT_EQ(srv.read(), "O"); // Logout Request on shutdown
  mgr.reset();
  delete om;
}
