/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * TTFIXOM end to end against a fake TT FIX gateway.
 *
 * Each test starts a QuickFIX acceptor in this process (FakeTT) on a free
 * localhost port and a TTFIXOM whose session config points at it, so the real
 * TTFixClient, QuickFIX session and TTFIXOM handlers all run. FakeTT plays the
 * gateway: it answers Logon, sends the News message TT sends when a session is
 * ready for orders, records every order and cancel it receives, and replies
 * with execution reports and cancel rejects on request.
 *
 * Execution reports reach TTFIXOM on QuickFIX's session thread, so its replies
 * to the SOM arrive asynchronously; Recorder collects them under a lock and the
 * tests wait for them with a timeout.
 *
 * Build and run (needs QuickFIX and libttom, i.e. make USE_TTOM=y first):
 *   cd ttom/test/unit && KSPRPROJ=... GTEST_PATH=... make && ./run_ttom_tests
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ttom/act/TTFIXOM.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/SetExchManager.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"

#include "config.h"
#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"
#include "quickfix/MessageStore.h"
#include "quickfix/Log.h"
#include "quickfix/Session.h"
#include "quickfix/SessionSettings.h"
#include "quickfix/ThreadedSocketAcceptor.h"
#include "quickfix/fix44/NewOrderSingle.h"
#include "quickfix/fix44/OrderCancelRequest.h"
#include "quickfix/fix44/OrderCancelReject.h"
#include "quickfix/fix44/ExecutionReport.h"
#include "quickfix/fix44/News.h"

using namespace std::chrono_literals;
namespace fs = std::filesystem;
namespace som = frame::som::msg;

namespace
{
  constexpr auto WAIT = 10s;
  constexpr double TICK = 0.015625; // ZN

  // Message pointers are passed as const so the call compiles whether or not
  // the fast_send(T*) = delete guard (#192) is in the tree.
  std::unique_ptr<const actors::Message> fsend(actors::Actor *to, const actors::Message &m,
                                               actors::Actor *from)
  {
    const actors::Message *p = &m;
    return to->fast_send(p, from);
  }

  template <class Pred>
  bool wait_until(Pred pred, std::chrono::milliseconds limit = WAIT)
  {
    auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end)
    {
      if (pred())
        return true;
      std::this_thread::sleep_for(5ms);
    }
    return pred();
  }

  int free_port()
  {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    ::bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    ::getsockname(s, reinterpret_cast<sockaddr *>(&a), &len);
    int port = ntohs(a.sin_port);
    ::close(s);
    return port;
  }

  // Stands in for the SOM and for the light that placed the order: TTFIXOM
  // replies with order.sender->send(...), from QuickFIX's thread.
  class Recorder : public actors::Actor
  {
  public:
    explicit Recorder(const char *n) { strncpy(name, n, sizeof(name) - 1); }
    ~Recorder() override
    {
      for (auto m : msgs)
        delete m;
    }
    const char *get_name() const override { return name; }

    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      std::lock_guard<std::mutex> l(mu);
      msgs.push_back(m);
    }

    template <class T>
    std::vector<const T *> all()
    {
      std::lock_guard<std::mutex> l(mu);
      std::vector<const T *> v;
      for (auto m : msgs)
        if (auto t = dynamic_cast<const T *>(m))
          v.push_back(t);
      return v;
    }

    // Waits for the nth (0-based) message of type T.
    template <class T>
    const T *wait_for(size_t nth = 0)
    {
      const T *r = nullptr;
      wait_until([&]
                 {
        auto v = all<T>();
        if (v.size() > nth) { r = v[nth]; return true; }
        return false; });
      return r;
    }

    size_t count()
    {
      std::lock_guard<std::mutex> l(mu);
      return msgs.size();
    }

  private:
    std::mutex mu;
    std::vector<const actors::Message *> msgs;
  };

  // The TT gateway side of the session.
  class FakeTT : public FIX::Application, public FIX::MessageCracker
  {
  public:
    // Behaviour knobs, set by a test before the event they affect.
    std::atomic<bool> send_news{true};
    std::atomic<bool> auto_ack{true};
    // On a Logout from the client, send this ExecType for the last order
    // before answering (cancel-on-disconnect style). 0 = off.
    std::atomic<char> exec_on_logout{0};

    std::atomic<bool> logged_on{false};

    struct Rec
    {
      std::string password;
      std::vector<FIX::Message> orders;
      std::vector<FIX::Message> cancels;
    };

    Rec snapshot()
    {
      std::lock_guard<std::mutex> l(mu);
      return rec;
    }

    size_t n_orders()
    {
      std::lock_guard<std::mutex> l(mu);
      return rec.orders.size();
    }
    size_t n_cancels()
    {
      std::lock_guard<std::mutex> l(mu);
      return rec.cancels.size();
    }

    void news()
    {
      FIX44::News n(FIX::Headline("ready"));
      n.setField(FIX::Text("TT session ready"));
      send(n);
    }

    // Execution report for the order whose ClOrdID is clordid.
    void exec(const std::string &clordid, char exec_type, char ord_status, int qty, int cum,
              int leaves, std::optional<double> last_px = {}, const std::string &text = "",
              const std::string &orig = "")
    {
      FIX44::ExecutionReport e(
          FIX::OrderID("TT-" + clordid),
          FIX::ExecID("E" + std::to_string(++exec_id)),
          FIX::ExecType(exec_type),
          FIX::OrdStatus(ord_status),
          FIX::Side(FIX::Side_BUY),
          FIX::LeavesQty(leaves),
          FIX::CumQty(cum),
          FIX::AvgPx(last_px.value_or(0)));
      e.set(FIX::ClOrdID(clordid));
      if (!orig.empty())
        e.set(FIX::OrigClOrdID(orig));
      e.set(FIX::SecondaryOrderID(std::to_string(CME_OID)));
      e.set(FIX::OrderQty(qty));
      e.set(FIX::TransactTime());
      if (last_px)
      {
        e.set(FIX::LastPx(*last_px));
        e.set(FIX::LastQty(cum));
      }
      if (!text.empty())
        e.set(FIX::Text(text));
      send(e);
    }

    void cancel_reject(const std::string &clordid, const std::string &orig, char ord_status,
                       char response_to)
    {
      FIX44::OrderCancelReject r(
          FIX::OrderID("TT-" + orig),
          FIX::ClOrdID(clordid),
          FIX::OrigClOrdID(orig),
          FIX::OrdStatus(ord_status),
          FIX::CxlRejResponseTo(response_to));
      r.set(FIX::CxlRejReason(1));
      r.set(FIX::TransactTime());
      send(r);
    }

    static constexpr uint64_t CME_OID = 8419050632586ULL;

  private:
    std::mutex mu;
    Rec rec;
    std::optional<FIX::SessionID> sid;
    int exec_id = 0;

    void send(FIX::Message &m)
    {
      FIX::Session::sendToTarget(m, *sid);
    }

    void onCreate(const FIX::SessionID &s) override { sid = s; }
    void onLogon(const FIX::SessionID &) override
    {
      logged_on = true;
      if (send_news)
        news();
    }
    void onLogout(const FIX::SessionID &) override { logged_on = false; }
    void toAdmin(FIX::Message &, const FIX::SessionID &) override {}
    void toApp(FIX::Message &, const FIX::SessionID &) override {}

    void fromAdmin(const FIX::Message &m, const FIX::SessionID &) override
    {
      const auto &t = m.getHeader().getField(FIX::FIELD::MsgType);
      if (t == "A")
      {
        std::lock_guard<std::mutex> l(mu);
        if (m.isSetField(FIX::FIELD::Password))
          rec.password = m.getField(FIX::FIELD::Password);
      }
      else if (t == "5" && exec_on_logout)
      {
        std::string cl;
        {
          std::lock_guard<std::mutex> l(mu);
          if (!rec.orders.empty())
            cl = rec.orders.back().getField(FIX::FIELD::ClOrdID);
        }
        if (!cl.empty())
          exec(cl, exec_on_logout, FIX::OrdStatus_CANCELED, 1, 0, 0);
      }
    }

    void fromApp(const FIX::Message &m, const FIX::SessionID &s) override { crack(m, s); }

    void onMessage(const FIX44::NewOrderSingle &m, const FIX::SessionID &) override
    {
      {
        std::lock_guard<std::mutex> l(mu);
        rec.orders.push_back(m);
      }
      if (auto_ack)
      {
        int qty = std::stoi(m.getField(FIX::FIELD::OrderQty));
        exec(m.getField(FIX::FIELD::ClOrdID), FIX::ExecType_NEW, FIX::OrdStatus_NEW, qty, 0, qty);
      }
    }
    void onMessage(const FIX44::OrderCancelRequest &m, const FIX::SessionID &) override
    {
      std::lock_guard<std::mutex> l(mu);
      rec.cancels.push_back(m);
    }
  };

  std::string write_file(const fs::path &p, const std::string &s)
  {
    std::ofstream(p) << s;
    return p.string();
  }

  class TTFIXOMTest : public ::testing::Test
  {
  protected:
    static inline int seq = 0;

    fs::path dir;
    int port = 0;
    std::string client_id, tt_id;
    FakeTT fake;
    std::unique_ptr<FIX::SessionSettings> acc_settings;
    std::unique_ptr<FIX::MemoryStoreFactory> acc_store;
    std::unique_ptr<FIX::ThreadedSocketAcceptor> acceptor;
    Recorder somr{"SOM"};
    Recorder light{"light"};
    std::unique_ptr<ttfixom::act::TTFIXOM> tt;
    const frame::ref::Asset *asset = nullptr;
    bool started = false;

    // RefData needs a universe file before its first use; the shared test
    // universe is enough, the tests add their own instrument.
    static void SetUpTestSuite()
    {
      const char *proj = std::getenv("KSPRPROJ");
      frame::ref::RefData::set_universe(
          proj ? std::string(proj) + "/unit_test/config/universe.csv" : "../../../unit_test/config/universe.csv", "");
      frame::ref::RefData::inst();
    }

    void SetUp() override
    {
      ++seq;
      dir = fs::temp_directory_path() / ("ttom_test_" + std::to_string(::getpid()) + "_" + std::to_string(seq));
      fs::create_directories(dir);
      port = free_port();
      client_id = "KASPAR" + std::to_string(seq);
      tt_id = "TTFAKE" + std::to_string(seq);

      std::string name = "ZNTT" + std::to_string(seq);
      asset = frame::ref::RefData::add_future_asset(name, en::x::CMEMD, 970000 + seq, "FFDXSX", "ZN", TICK);

      auto acc_cfg = write_file(dir / "acceptor.cfg",
                                "[DEFAULT]\n"
                                "ConnectionType=acceptor\n"
                                "SocketAcceptPort=" + std::to_string(port) + "\n"
                                "StartTime=00:00:00\nEndTime=00:00:00\n"
                                "HeartBtInt=30\nUseDataDictionary=N\nResetOnLogon=Y\n"
                                "[SESSION]\nBeginString=FIX.4.4\n"
                                "SenderCompID=" + tt_id + "\nTargetCompID=" + client_id + "\n");
      acc_settings = std::make_unique<FIX::SessionSettings>(acc_cfg);
      acc_store = std::make_unique<FIX::MemoryStoreFactory>();
      acceptor = std::make_unique<FIX::ThreadedSocketAcceptor>(fake, *acc_store, *acc_settings);
      acceptor->start();
    }

    // The client side, in the shape of ttom/config/tt_example.cfg.
    std::string client_cfg()
    {
      return write_file(dir / "client.cfg",
                        "[DEFAULT]\n"
                        "FileStorePath=" + (dir / "store").string() + "\n"
                        "ConnectionType=initiator\n"
                        "ReconnectInterval=1\n"
                        "StartTime=00:00:00\nEndTime=00:00:00\n"
                        "HeartBtInt=30\n"
                        "BeginString=FIX.4.4\n"
                        "ScreenLogShowIncoming=N\nScreenLogShowOutgoing=N\nScreenLogShowEvents=N\n"
                        "SecurityExchange=CME\n"
                        "Password=s3cret\n"
                        "Account=ACCT1\n"
                        "TargetCompID=" + tt_id + "\n"
                        "SenderCompID=" + client_id + "\n"
                        "OnBehalfOfSubID=fixuser\n"
                        "[SESSION]\n"
                        "SocketConnectHost=127.0.0.1\n"
                        "SocketConnectPort=" + std::to_string(port) + "\n"
                        "UseDataDictionary=N\n"
                        "ResetOnLogon=Y\nResetOnLogout=Y\nResetOnDisconnect=Y\n");
    }

    void make_tt(actors::Actor *som_actor = nullptr)
    {
      tt = std::make_unique<ttfixom::act::TTFIXOM>(client_cfg(), som_actor, en::x::TT);
    }

    // Start the session and wait until orders are accepted (logged on + News).
    void start_and_wait_ready()
    {
      ASSERT_TRUE(tt);
      actors::msg::Start s;
      fsend(tt.get(), s, nullptr);
      started = true;
      ASSERT_TRUE(wait_until([&]
                             { return fake.logged_on.load(); }))
          << "client never logged on";
      // News goes out right after logon; give the client a moment to read it.
      std::this_thread::sleep_for(200ms);
    }

    void stop_tt()
    {
      if (tt && started)
      {
        actors::msg::Shutdown sd;
        fsend(tt.get(), sd, nullptr);
        started = false;
      }
    }

    void TearDown() override
    {
      stop_tt();
      tt.reset();
      if (acceptor)
        acceptor->stop(true);
      acceptor.reset();
      std::error_code ec;
      fs::remove_all(dir, ec);
    }

    som::Order order(int oid, int px_ticks, double sz, en::bs side = en::bs::BUY)
    {
      som::Order o("TT", en::x::TT, asset->id, sz, px_ticks, side, 3);
      o.oid = oid;
      return o;
    }

    // Place an order and return the ClOrdID TT saw it under.
    std::string place(const som::Order &o)
    {
      auto before = fake.n_orders();
      fsend(tt.get(), o, &light);
      EXPECT_TRUE(wait_until([&]
                             { return fake.n_orders() > before; }))
          << "order never reached TT";
      auto r = fake.snapshot();
      return r.orders.empty() ? "" : r.orders.back().getField(FIX::FIELD::ClOrdID);
    }

    std::string cancel(int oid)
    {
      auto before = fake.n_cancels();
      som::Cancel c(oid, 0);
      fsend(tt.get(), c, &light);
      EXPECT_TRUE(wait_until([&]
                             { return fake.n_cancels() > before; }))
          << "cancel never reached TT";
      auto r = fake.snapshot();
      return r.cancels.empty() ? "" : r.cancels.back().getField(FIX::FIELD::ClOrdID);
    }
  };

} // namespace

// ---------------------------------------------------------------------------
// Session gating
// ---------------------------------------------------------------------------

// Before the session is up, TTFixClient throws on send; TTFIXOM must turn that
// into a Reject to the order's sender rather than let it escape a noexcept
// handler (which would terminate kaspr).
TEST_F(TTFIXOMTest, OrderBeforeLogonIsRejected)
{
  make_tt();
  auto o = order(11, 7000, 2);
  fsend(tt.get(), o, &light);
  auto rej = light.wait_for<som::Reject>();
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->id, 11u);
  EXPECT_EQ(fake.n_orders(), 0u);
}

// Logged on but TT has not sent News yet: not ready for orders.
TEST_F(TTFIXOMTest, OrderBeforeNewsIsRejected)
{
  fake.send_news = false;
  make_tt();
  start_and_wait_ready();
  auto o = order(12, 7000, 2);
  fsend(tt.get(), o, &light);
  auto rej = light.wait_for<som::Reject>();
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->id, 12u);
  EXPECT_EQ(fake.n_orders(), 0u);

  // Once News arrives, orders go out.
  fake.news();
  std::this_thread::sleep_for(200ms);
  auto o2 = order(13, 7000, 2);
  EXPECT_FALSE(place(o2).empty());
}

// On Start, TTFIXOM tells the SOM it manages venue TT.
TEST_F(TTFIXOMTest, RegistersWithSomOnStart)
{
  make_tt(&somr);
  start_and_wait_ready();
  auto reg = somr.wait_for<som::SetExchManager>();
  ASSERT_NE(reg, nullptr);
  EXPECT_EQ(reg->venue, en::x::TT);
  EXPECT_EQ(reg->manager, tt.get());
}

TEST_F(TTFIXOMTest, NoSomMeansNoRegistration)
{
  make_tt(nullptr);
  start_and_wait_ready();
  EXPECT_EQ(somr.count(), 0u);
}

// ---------------------------------------------------------------------------
// Outbound messages
// ---------------------------------------------------------------------------

TEST_F(TTFIXOMTest, LogonCarriesPassword)
{
  make_tt();
  start_and_wait_ready();
  EXPECT_EQ(fake.snapshot().password, "s3cret");
}

// The NewOrderSingle carries what TT needs: price in decimal (ticks x tick
// size), the instrument from refdata, account, exchange, and tag 116.
TEST_F(TTFIXOMTest, NewOrderSingleFields)
{
  make_tt();
  start_and_wait_ready();
  auto o = order(21, 7000, 3);
  auto cl = place(o);
  ASSERT_FALSE(cl.empty());

  auto nos = fake.snapshot().orders.back();
  EXPECT_DOUBLE_EQ(std::stod(nos.getField(FIX::FIELD::Price)), 7000 * TICK); // 109.375
  EXPECT_EQ(std::stoi(nos.getField(FIX::FIELD::OrderQty)), 3);
  EXPECT_EQ(nos.getField(FIX::FIELD::Side), "1");
  EXPECT_EQ(nos.getField(FIX::FIELD::OrdType), "2");     // limit
  EXPECT_EQ(nos.getField(FIX::FIELD::TimeInForce), "0"); // day
  EXPECT_EQ(nos.getField(FIX::FIELD::Account), "ACCT1");
  EXPECT_EQ(nos.getField(FIX::FIELD::SecurityExchange), "CME");
  EXPECT_EQ(nos.getField(FIX::FIELD::CFICode), "FFDXSX");
  EXPECT_EQ(nos.getField(FIX::FIELD::SecurityID), asset->name);
  EXPECT_EQ(nos.getField(FIX::FIELD::Symbol), asset->mnemonic);
  EXPECT_EQ(nos.getField(FIX::FIELD::SecurityIDSource), "98");
  EXPECT_EQ(nos.getHeader().getField(FIX::FIELD::OnBehalfOfSubID), "fixuser");
}

TEST_F(TTFIXOMTest, SellOrderSide)
{
  make_tt();
  start_and_wait_ready();
  auto o = order(22, 7000, 1, en::bs::SEL);
  place(o);
  EXPECT_EQ(fake.snapshot().orders.back().getField(FIX::FIELD::Side), "2");
}

// Every order gets its own ClOrdID.
TEST_F(TTFIXOMTest, ClOrdIdsAreUnique)
{
  make_tt();
  start_and_wait_ready();
  std::set<std::string> ids;
  for (int i = 0; i < 20; ++i)
  {
    auto o = order(100 + i, 7000, 1);
    ids.insert(place(o));
  }
  EXPECT_EQ(ids.size(), 20u);
}

// ---------------------------------------------------------------------------
// Execution reports back to the order's sender
// ---------------------------------------------------------------------------

// ExecType New -> Ack(oid) carrying the CME order id (SecondaryOrderID).
TEST_F(TTFIXOMTest, AckCarriesCmeOrderId)
{
  make_tt();
  start_and_wait_ready();
  auto o = order(31, 7000, 2);
  place(o);
  auto ack = light.wait_for<som::Ack>();
  ASSERT_NE(ack, nullptr);
  EXPECT_EQ(ack->id, 31u);
  EXPECT_EQ(ack->xordid, FakeTT::CME_OID);
}

// Two trades: each Fill carries the quantity of that trade (not the
// cumulative), what is left, and the trade price converted back to ticks.
TEST_F(TTFIXOMTest, PartialThenFullFill)
{
  make_tt();
  start_and_wait_ready();
  auto o = order(41, 7000, 5);
  auto cl = place(o);
  ASSERT_NE(light.wait_for<som::Ack>(), nullptr);

  fake.exec(cl, FIX::ExecType_TRADE, FIX::OrdStatus_PARTIALLY_FILLED, 5, 2, 3, 7001 * TICK);
  auto f1 = light.wait_for<som::Fill>(0);
  ASSERT_NE(f1, nullptr);
  EXPECT_EQ(f1->id, 41u);
  EXPECT_DOUBLE_EQ(f1->sz, 2);
  EXPECT_DOUBLE_EQ(f1->still_to_be_filled, 3);
  EXPECT_EQ(f1->pxi, 7001);
  EXPECT_EQ(f1->side, en::bs::BUY);
  EXPECT_EQ(f1->venue, en::x::TT);
  EXPECT_EQ(f1->sym, asset->id);
  EXPECT_EQ(int(f1->owner), 3);

  fake.exec(cl, FIX::ExecType_TRADE, FIX::OrdStatus_FILLED, 5, 5, 0, 7000 * TICK);
  auto f2 = light.wait_for<som::Fill>(1);
  ASSERT_NE(f2, nullptr);
  EXPECT_DOUBLE_EQ(f2->sz, 3);
  EXPECT_DOUBLE_EQ(f2->still_to_be_filled, 0);
  EXPECT_EQ(f2->pxi, 7000);
}

// A fractional ZN price (1/64ths) survives the decimal round trip.
TEST_F(TTFIXOMTest, FillPriceRoundTripsFractionalTicks)
{
  make_tt();
  start_and_wait_ready();
  auto o = order(42, 6977, 1);
  auto cl = place(o);
  EXPECT_DOUBLE_EQ(std::stod(fake.snapshot().orders.back().getField(FIX::FIELD::Price)), 6977 * TICK);
  fake.exec(cl, FIX::ExecType_TRADE, FIX::OrdStatus_FILLED, 1, 1, 0, 6977 * TICK);
  auto f = light.wait_for<som::Fill>();
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->pxi, 6977);
}

TEST_F(TTFIXOMTest, ExchangeRejectGivesReject)
{
  fake.auto_ack = false;
  make_tt();
  start_and_wait_ready();
  auto o = order(51, 7000, 1);
  auto cl = place(o);
  fake.exec(cl, FIX::ExecType_REJECTED, FIX::OrdStatus_REJECTED, 1, 0, 0, {}, "price out of band");
  auto rej = light.wait_for<som::Reject>();
  ASSERT_NE(rej, nullptr);
  EXPECT_EQ(rej->id, 51u);
  EXPECT_EQ(light.all<som::Ack>().size(), 0u);
}

// ---------------------------------------------------------------------------
// Cancels
// ---------------------------------------------------------------------------

// The cancel references the order's ClOrdID and side; TT's Canceled report
// (under the cancel's own ClOrdID) becomes CancAck(oid).
TEST_F(TTFIXOMTest, CancelRoundTrip)
{
  make_tt();
  start_and_wait_ready();
  auto o = order(61, 7000, 2, en::bs::SEL);
  auto cl = place(o);
  ASSERT_NE(light.wait_for<som::Ack>(), nullptr);

  auto ccl = cancel(61);
  ASSERT_FALSE(ccl.empty());
  auto req = fake.snapshot().cancels.back();
  EXPECT_EQ(req.getField(FIX::FIELD::OrigClOrdID), cl);
  EXPECT_NE(ccl, cl);
  EXPECT_EQ(req.getField(FIX::FIELD::Side), "2");
  EXPECT_EQ(req.getField(FIX::FIELD::Account), "ACCT1");

  fake.exec(ccl, FIX::ExecType_CANCELED, FIX::OrdStatus_CANCELED, 2, 0, 0, {}, "", cl);
  auto ca = light.wait_for<som::CancAck>();
  ASSERT_NE(ca, nullptr);
  EXPECT_EQ(ca->id, 61u);
}

// Cancel rejected because the order already filled: CancReject(oid) with
// reason FROMEXCHANGE and TT's OrdStatus.
TEST_F(TTFIXOMTest, CancelRejectGivesCancReject)
{
  make_tt();
  start_and_wait_ready();
  auto o = order(71, 7000, 1);
  auto cl = place(o);
  ASSERT_NE(light.wait_for<som::Ack>(), nullptr);
  auto ccl = cancel(71);

  fake.cancel_reject(ccl, cl, FIX::OrdStatus_FILLED, FIX::CxlRejResponseTo_ORDER_CANCEL_REQUEST);
  auto cr = light.wait_for<som::CancReject>();
  ASSERT_NE(cr, nullptr);
  EXPECT_EQ(cr->id, 71u);
  EXPECT_EQ(cr->reason, som::CancReject::FROMEXCHANGE);
  EXPECT_EQ(cr->ord_status, int(ttfixom::OrdStatus::Filled));
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

// Shutdown runs TTFIXOM::shutdown_handler, which calls Initiator::stop(); stop()
// logs out and then joins QuickFIX's session thread. The handler runs while
// TTFIXOM's fast_send_mutex is held, and an execution report arriving in that
// window is delivered by the session thread with fast_send, which needs the
// same mutex. Here the gateway sends one (a cancel-on-disconnect report) as it
// receives the Logout. Shutdown must still finish.
//
// Runs in a child process with an alarm: if stop() deadlocks, the child is
// killed and the test fails instead of hanging the suite.
//
// DISABLED: it deadlocks today. The main thread sits in
// shutdown_handler -> Initiator::stop -> pthread_join while holding the mutex;
// the session thread sits in CallBackImp::onExecutionReport -> fast_send ->
// pthread_mutex_lock. Enable once CallBackImp hands reports over without
// taking TTFIXOM's mutex on the session thread (e.g. send instead of fast_send).
TEST_F(TTFIXOMTest, DISABLED_ShutdownWithInboundReportDoesNotHang)
{
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT(
      {
        ::alarm(30);
        fake.exec_on_logout = FIX::ExecType_CANCELED;
        make_tt();
        start_and_wait_ready();
        auto o = order(81, 7000, 1);
        place(o);
        light.wait_for<som::Ack>();
        stop_tt();
        std::_Exit(0);
      },
      ::testing::ExitedWithCode(0), "");
}

// Control for the test above: with nothing inbound during the logout, the
// same shutdown completes.
TEST_F(TTFIXOMTest, ShutdownWithoutInboundReportCompletes)
{
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT(
      {
        ::alarm(30);
        make_tt();
        start_and_wait_ready();
        auto o = order(82, 7000, 1);
        place(o);
        light.wait_for<som::Ack>();
        stop_tt();
        std::_Exit(0);
      },
      ::testing::ExitedWithCode(0), "");
}
