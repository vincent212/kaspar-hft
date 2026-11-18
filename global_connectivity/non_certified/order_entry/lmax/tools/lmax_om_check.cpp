/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// lmax_om_check: the live order-entry check, in LMAX's demo or UAT only.
//
//   lmax_om_check OM.ini MD.ini SEC_ID     against the hosts in the configs
//   lmax_om_check --sim                    against SimLmaxGateway on 127.0.0.1
//
// SEC_ID is the Kaspar asset id of the instrument (as in the instruments
// file). Steps, each waiting for its answer (10 s at most):
//   1. read the best bid and offer from LMAX market data
//   2. a buy limit of one unit 2% below the bid (far from the market): Ack
//   3. amend it to two units, one tick higher: CancAck (old) + Ack (new)
//   4. cancel it: CancAck
//   5. a buy limit IOC of one unit five ticks above the offer: Ack + Fill
//      (or CancAck if LMAX expired it unfilled)
// Every SOM message is then matched against LMAX's ExecutionReports, which
// the tool records as they arrive: ExecType, ClOrdID, quantities (units x
// contracts per unit) and prices (ticks x tick). It prints both and PASS or
// FAIL. The configs must be environment demo or uat; production hosts are
// refused before anything connects.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>

#include "actors/act/Manager.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Reject.hpp"

#include "lmax_fix/SimLmaxGateway.hpp"
#include "lmax_md/act/LmaxMdFeed.hpp"
#include "lmax_om/act/LmaxOM.hpp"

using namespace lmax;
using namespace std::chrono_literals;
namespace som = frame::som::msg;

namespace
{
  std::mutex g_mu;
  std::vector<std::string> g_som;         // SOM messages, in order
  std::vector<om::ExecReport> g_execs;    // LMAX reports, in order
  double g_bid = 0, g_ask = 0;

  class Som : public actors::Actor
  {
  public:
    Som() { strncpy(name, "SOM", sizeof(name) - 1); }
    const char *get_name() const override { return name; }
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      std::string s;
      if (auto a = dynamic_cast<const som::Ack *>(m))
        s = "Ack " + std::to_string(a->id);
      else if (auto f = dynamic_cast<const som::Fill *>(m))
        s = "Fill " + std::to_string(f->id) + " " + std::to_string(int(f->sz)) + " @" + std::to_string(f->pxi);
      else if (auto c = dynamic_cast<const som::CancAck *>(m))
        s = "CancAck " + std::to_string(c->id);
      else if (auto r = dynamic_cast<const som::CancReject *>(m))
        s = "CancReject " + std::to_string(r->id) + " reason " + std::to_string(r->reason);
      else if (auto j = dynamic_cast<const som::Reject *>(m))
        s = "Reject " + std::to_string(j->id) + " reason " + std::to_string(j->reason);
      delete m;
      if (s.empty())
        return;
      std::lock_guard<std::mutex> l(g_mu);
      g_som.push_back(s);
      std::cout << "SOM   <- " << s << std::endl;
    }
  };

  // Records every ExecutionReport on its way to the OM.
  class Tee : public om::SessionEvents
  {
  public:
    explicit Tee(om::SessionEvents &in) : in_(in) {}
    void logged_on() override { in_.logged_on(); }
    void logged_out(const std::string &r) override { in_.logged_out(r); }
    void exec(const om::ExecReport &r) override
    {
      {
        std::lock_guard<std::mutex> l(g_mu);
        g_execs.push_back(r);
      }
      std::cout << "LMAX  -> 8 ExecType " << r.exec_type << " OrdStatus " << r.ord_status << " ClOrdID " << r.clordid
                << " Orig " << r.orig_clordid << " OrderID " << r.order_id << " ExecID " << r.exec_id << " qty "
                << r.qty << " cum " << r.cum << " leaves " << r.leaves << " last " << r.last_qty << "@" << r.last_px
                << (r.text.empty() ? "" : " text " + r.text) << std::endl;
      in_.exec(r);
    }
    void cancel_reject(const om::CancelReject &r) override
    {
      std::cout << "LMAX  -> 9 ClOrdID " << r.clordid << " Orig " << r.orig_clordid << " reason " << r.reason << " "
                << r.text << std::endl;
      in_.cancel_reject(r);
    }
    void session_reject(const std::string &t) override
    {
      std::cout << "LMAX  -> 3 " << t << std::endl;
      in_.session_reject(t);
    }
    void unparsed(const std::string &t) override
    {
      std::cout << "LMAX  -> unparsed " << t << std::endl;
      in_.unparsed(t);
    }

  private:
    om::SessionEvents &in_;
  };

  class TeeSession : public om::LmaxSession
  {
  public:
    TeeSession(std::unique_ptr<Tee> t, std::unique_ptr<om::LmaxSession> s) : tee_(std::move(t)), s_(std::move(s)) {}
    ~TeeSession() override { s_.reset(); }
    void start() override { s_->start(); }
    void stop() override { s_->stop(); }
    om::CallStatus send_new(const om::NewOrderReq &r) override { return s_->send_new(r); }
    om::CallStatus send_cancel(const om::CancelReq &r) override { return s_->send_cancel(r); }
    om::CallStatus send_replace(const om::ReplaceReq &r) override { return s_->send_replace(r); }
    om::CallStatus send_status(const om::StatusReq &r) override { return s_->send_status(r); }
    om::CallStatus send_tcr_request(const om::TcrReq &r) override { return s_->send_tcr_request(r); }

  private:
    std::unique_ptr<Tee> tee_;
    std::unique_ptr<om::LmaxSession> s_;
  };

  class Top : public md::MdTap
  {
  public:
    explicit Top(std::string id) : id_(std::move(id)) {}
    void on_snapshot(size_t, const md::MdSnapshot &s, const md::LmaxMdSync &sync) override
    {
      if (s.security_id != id_ || sync.bids().empty() || sync.asks().empty())
        return;
      std::lock_guard<std::mutex> l(g_mu);
      g_bid = sync.bids().front().first;
      g_ask = sync.asks().front().first;
    }

  private:
    std::string id_;
  };

  bool wait_som(size_t n, std::chrono::seconds limit = 10s)
  {
    auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end)
    {
      {
        std::lock_guard<std::mutex> l(g_mu);
        if (g_som.size() >= n)
          return true;
      }
      std::this_thread::sleep_for(20ms);
    }
    return false;
  }
}

int main(int argc, char **argv)
{
  const bool sim = argc == 2 && std::string(argv[1]) == "--sim";
  if (!sim && argc != 4)
  {
    std::cerr << "usage: lmax_om_check OM.ini MD.ini SEC_ID | --sim" << std::endl;
    return 2;
  }
  om::LmaxOMConfig ocfg;
  md::MdFeedConfig mcfg;
  en::x venue = en::x::UNI;
  int sym = 0;
  std::unique_ptr<fix::SimLmaxGateway> gw;
  const char *k = std::getenv("KSPRPROJ");
  frame::ref::RefData::set_universe(std::string(k ? k : "") + "/unit_test/config/universe.csv", "");
  frame::ref::RefData::inst();
  if (sim)
  {
    fix::SimLmaxGateway::Config g;
    g.md_dictionary = LMAX_MD_DICT;
    g.trading_dictionary = LMAX_TRADING_DICT;
    g.instruments = {{"4001", 1.15230, 0.00001, 0.1}};
    gw = std::make_unique<fix::SimLmaxGateway>(g);
    gw->start();
    auto a = frame::ref::RefData::add_future_asset("EURUSD_SIM", en::x::UNI, 870001, "FXXXXX", "EURUSD_SIM", 0.00001);
    sym = int(a->id);
    auto &p = ocfg.session.params;
    p.env = fix::Env::Loopback;
    p.host = "127.0.0.1";
    p.port = gw->port();
    p.tls = false;
    p.sender = "CLIENTTR";
    p.target = "LMXBD";
    p.dictionary = LMAX_TRADING_DICT;
    p.reset_on_logon = true;
    p.start_time = p.end_time = "00:00:00";
    ocfg.session.username = "user";
    ocfg.session.password = "pass";
    ocfg.instruments[sym] = om::OmInstrument{"4001", "EUR/USD", 0.00001, 0.1};
    mcfg.session = p;
    mcfg.session.sender = "CLIENTMD";
    mcfg.session.target = "LMXBDM";
    mcfg.session.dictionary = LMAX_MD_DICT;
    mcfg.username = "user";
    mcfg.password = "pass";
    mcfg.instruments = {{"4001", "EUR/USD", 7001, 0.00001, 0.1}};
  }
  else
  {
    std::string err;
    if (!om::LmaxOMConfig::from_file(argv[1], ocfg, venue, err) || !md::MdFeedConfig::from_file(argv[2], mcfg, err))
    {
      std::cerr << "lmax_om_check: " << err << std::endl;
      return 2;
    }
    if (ocfg.session.params.env != fix::Env::Demo && ocfg.session.params.env != fix::Env::Uat)
    {
      std::cerr << "lmax_om_check: only the demo and UAT environments" << std::endl;
      return 2;
    }
    sym = std::atoi(argv[3]);
    if (!ocfg.instruments.count(sym))
    {
      std::cerr << "lmax_om_check: asset " << sym << " is not in the OM's instruments" << std::endl;
      return 2;
    }
    // The Fill message needs the asset in RefData.
    auto &in = ocfg.instruments[sym];
    if (!frame::ref::RefData::get_asset(std::size_t(sym)))
      std::cerr << "lmax_om_check: asset " << sym << " (" << in.symbol << ") not in RefData; Fill prices print as ticks"
                << std::endl;
  }
  const om::OmInstrument in = ocfg.instruments[sym];

  Top top(in.security_id);
  auto *feed = new md::LmaxMdFeed(mcfg, std::vector<actors::Actor *>(mcfg.instruments.size(), nullptr), &top);
  Som somr;
  auto inner = om::qf_session_factory(ocfg.session);
  om::SessionFactory tee = [inner](om::SessionEvents &ev) -> std::unique_ptr<om::LmaxSession> {
    auto t = std::make_unique<Tee>(ev);
    auto s = inner(*t);
    return std::make_unique<TeeSession>(std::move(t), std::move(s));
  };
  auto *omgr = new om::LmaxOM(ocfg, &somr, tee, venue);
  actors::Manager mgr("lmax_om_check");
  mgr.manage_and_start(feed);
  mgr.manage_and_start(omgr);

  bool ok = true;
  auto fail = [&](const char *what) {
    std::cout << "FAIL: " << what << std::endl;
    ok = false;
  };
  auto deadline = std::chrono::steady_clock::now() + 30s;
  while ((!omgr->ready() || g_bid == 0) && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(50ms);
  double bid, ask;
  {
    std::lock_guard<std::mutex> l(g_mu);
    bid = g_bid;
    ask = g_ask;
  }
  if (!omgr->ready() || bid == 0)
    fail("no logon or no market data within 30 s");
  else
  {
    std::cout << "market " << bid << " / " << ask << std::endl;
    const int far = int(std::llround(bid * 0.98 / in.tick));
    som::Order o("FX", venue, sym, 1, far, en::bs::BUY, 1);
    o.oid = 1;
    omgr->send(new som::Order(o), &somr);
    if (!wait_som(1))
      fail("no answer to the far order");
    som::Order r("FX", venue, sym, 2, far + 1, en::bs::BUY, 1);
    r.oid = 2;
    r.oid_to_cancel = 1;
    omgr->send(new som::Order(r), &somr);
    if (!wait_som(3))
      fail("no answer to the amend");
    omgr->send(new som::Cancel(2), &somr);
    if (!wait_som(4))
      fail("no answer to the cancel");
    som::Order f("FX", venue, sym, 1, int(std::llround(ask / in.tick)) + 5, en::bs::BUY, 1);
    f.oid = 3;
    f.tif = som::Order::TimeInForce::IOC;
    omgr->send(new som::Order(f), &somr);
    if (!wait_som(6))
      fail("no answer to the marketable order");
    std::this_thread::sleep_for(1s);
  }
  mgr.end();

  // Match the SOM messages against LMAX's reports.
  std::lock_guard<std::mutex> l(g_mu);
  const std::vector<std::string> want = {"Ack 1", "CancAck 1", "Ack 2", "CancAck 2", "Ack 3"};
  for (size_t i = 0; i < want.size(); ++i)
    if (i >= g_som.size() || g_som[i] != want[i])
    {
      std::cout << "SOM message " << i << ": expected " << want[i] << ", got "
                << (i < g_som.size() ? g_som[i] : "nothing") << std::endl;
      ok = false;
    }
  // Every Ack, CancAck and Fill has its LMAX report.
  int news = 0, replaced = 0, canceled = 0, trades = 0;
  for (auto &e : g_execs)
  {
    news += e.exec_type == '0';
    replaced += e.exec_type == '5';
    canceled += e.exec_type == '4' || e.exec_type == 'C';
    trades += e.exec_type == 'F';
  }
  std::cout << "LMAX reports: New " << news << ", Replaced " << replaced << ", Canceled/Expired " << canceled
            << ", Trade " << trades << std::endl;
  if (news != 2 || replaced != 1)
    ok = false;
  for (auto &s : g_som)
    if (s.rfind("Fill 3 ", 0) == 0)
    {
      const om::ExecReport *t = nullptr;
      for (auto &e : g_execs)
        if (e.exec_type == 'F')
          t = &e;
      int units = 0, pxi = 0;
      std::sscanf(s.c_str(), "Fill 3 %d @%d", &units, &pxi);
      if (!t || std::fabs(t->last_qty - units * in.unit) > 1e-9 || std::llround(t->last_px / in.tick) != pxi)
      {
        std::cout << "Fill does not match LMAX's trade report" << std::endl;
        ok = false;
      }
    }
  if (gw)
    gw->stop();
  std::cout << (ok ? "PASS" : "FAIL") << std::endl;
  return ok ? 0 : 1;
}
