/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "coinbase_exchange_pt/act/CbxDropClient.hpp"

#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "coinbase_exchange/Num.hpp"
#include "enum/e_names.hpp"

namespace coinbase_exchange::pt
{
using namespace fix;
using oe::msg::CbxReport;
namespace pt_msg = post_trade::msg;

// ---- mapping -----------------------------------------------------------------------------------

std::optional<pt_msg::TradeCapture> to_trade_capture(const FixMsg &er, const DropConfig &cfg, std::string &why)
{
  if (er.type() != "8")
  {
    why = "not an ExecutionReport";
    return std::nullopt;
  }
  const char et = er.get_char(ExecType);
  if (et != 'F' && et != 'G' && et != 'H')
  {
    why = std::string("order event, ExecType ") + et;
    return std::nullopt;
  }
  pt_msg::TradeCapture t;
  t.venue = en::x::COINBASEX;
  t.symbol = er.get(Symbol);
  auto pit = cfg.products.find(t.symbol);
  int size_decimals = pit != cfg.products.end() ? pit->second.size_decimals : 8;
  t.asset = pit != cfg.products.end() ? pit->second.asset : -1;
  const std::string order_id = er.get(OrderID);
  const std::string trade = er.get(TradeID);
  const std::string exec = er.get(ExecID);
  if (exec.empty() || order_id.empty())
  {
    why = "no ExecID or OrderID";
    return std::nullopt;
  }
  // TradeID is per product and names the trade on both sides; with the order
  // id it is our side of it. Without TradeID (before 2025-10, the changelog),
  // the ExecID is all there is.
  t.trade_id = trade.empty() ? exec : t.symbol + "/" + trade + "/" + order_id;
  t.report_id = exec;
  t.report_seq = 0; // MsgSeqNum restarts with every logon (ResetSeqNumFlag): arrival order is used
  t.event = et == 'F' ? post_trade::Event::New : et == 'G' ? post_trade::Event::Amend : post_trade::Event::Bust;
  if (et != 'F')
    t.ref_trade_id = t.trade_id;
  t.venue_order_id = order_id;
  const char side = er.get_char(Side);
  t.side = side == '1' ? post_trade::Side::Buy : side == '2' ? post_trade::Side::Sell : post_trade::Side::Unknown;
  if (et == 'F' || et == 'G')
  {
    auto q = scaled(er.get(LastQty), size_decimals);
    const std::string px = er.get(LastPx);
    int px_dec = decimals_of(px);
    auto pm = px_dec >= 0 ? scaled(px, px_dec) : std::nullopt;
    if (!q || !pm)
    {
      why = "LastQty or LastPx missing or malformed";
      return std::nullopt;
    }
    t.qty = *q; // in the product's base increments (feed.md section 7)
    t.price_mantissa = *pm;
    t.price_decimals = px_dec;
  }
  const uint64_t tx = parse_fix_time(er.get(TransactTime));
  if (tx)
  {
    time_t secs = time_t(tx / 1000000000ull);
    std::tm tm{};
    gmtime_r(&secs, &tm);
    t.trade_date = uint32_t((tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
    t.trade_time_ns = int64_t(tx % (86400ull * 1000000000ull));
  }
  t.poss_dup = er.get_char(PossDupFlag) == 'Y' || er.get_char(PossResend) == 'Y';
  t.raw = er.str();
  return t;
}

// ---- configuration -----------------------------------------------------------------------------

bool DropConfig::from_file(const std::string &path, DropConfig &out, std::string &err)
{
  boost::property_tree::ptree pt;
  try
  {
    boost::property_tree::read_ini(path, pt);
    auto env = parse_environment(pt.get<std::string>("session.environment", "sandbox"));
    if (!env)
    {
      err = "session.environment must be sandbox or production";
      return false;
    }
    auto expand = [](std::string p) {
      if (!p.empty() && p[0] == '~')
        if (const char *h = std::getenv("HOME"))
          p = std::string(h) + p.substr(1);
      return p;
    };
    auto &s = out.session;
    s.environment = *env;
    s.drop_copy = true;
    s.cancel_on_disconnect = 0;
    s.host = pt.get<std::string>("session.host", "");
    s.port = pt.get<int>("session.port", 0);
    s.key_file = expand(pt.get<std::string>("session.key_file", default_key_path()));
    s.heartbeat_s = pt.get<int>("session.heartbeat_s", 30);
    s.store_dir = expand(pt.get<std::string>("session.store_dir", ""));
    s.ca_file = expand(pt.get<std::string>("session.ca_file", ""));
    s.transport_dict = expand(pt.get<std::string>("session.transport_dict", ""));
    s.app_dict = expand(pt.get<std::string>("session.app_dict", ""));
    out.reconnect_ms = pt.get<int>("session.reconnect_ms", 5000);
    std::stringstream list(pt.get<std::string>("products.list", ""));
    std::string prod;
    while (std::getline(list, prod, ','))
      if (!prod.empty())
        out.products[prod] = {pt.get<int>(prod + ".asset", -1),
                              decimals_of(pt.get<std::string>(prod + ".base_increment", "0.00000001"))};
  }
  catch (const std::exception &e)
  {
    err = e.what();
    return false;
  }
  if (out.session.heartbeat_s <= 0 || out.session.heartbeat_s > 300)
  {
    err = "heartbeat_s must be 1 to 300 (drop copy)";
    return false;
  }
  return true;
}

// ---- the actor -----------------------------------------------------------------------------------

class CbxDropClient::Events : public oe::SessionEvents
{
public:
  Events(CbxDropClient *c, uint64_t s) : c_(c), s_(s) {}
  void logged_on() override { post(CbxReport::LOGGED_ON); }
  void logon_rejected(const std::string &why) override { post(CbxReport::LOGON_REJECTED, why); }
  void disconnected(const std::string &why) override { post(CbxReport::DISCONNECTED, why); }
  void busy() override {}
  void available() override {}
  void message(const FixMsg &m) override
  {
    auto r = new CbxReport(CbxReport::MESSAGE, s_);
    r->fix = m;
    c_->send(r, nullptr);
  }

private:
  void post(CbxReport::Kind k, std::string t = {})
  {
    auto r = new CbxReport(k, s_);
    r->text = std::move(t);
    c_->send(r, nullptr);
  }
  CbxDropClient *c_;
  uint64_t s_;
};

CbxDropClient::CbxDropClient(DropConfig cfg, oe::SessionFactory factory, actors::Actor *subscriber)
    : cfg_(std::move(cfg)), factory_(std::move(factory)), subscriber_(subscriber)
{
  cfg_.session.drop_copy = true;
  cfg_.session.cancel_on_disconnect = 0;
  strncpy(name, "CbxDropClient", sizeof(name) - 1);
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
  MESSAGE_HANDLER(CbxReport, report_handler);
}

CbxDropClient::~CbxDropClient()
{
  stopping_ = true;
  reconnect_timer_ = {};
  session_.reset();
  events_.reset();
}

void CbxDropClient::connect() noexcept
{
  ++session_no_;
  events_ = std::make_unique<Events>(this, session_no_);
  try
  {
    session_ = factory_(cfg_.session, *events_);
    session_->logon();
  }
  catch (const std::exception &e)
  {
    std::cerr << "CbxDropClient: cannot create the session: " << e.what() << std::endl;
    session_.reset();
    schedule_reconnect();
  }
}

void CbxDropClient::schedule_reconnect() noexcept
{
  if (stopping_ || rejected_)
    return;
  auto expected = session_no_;
  auto ms = cfg_.reconnect_ms;
  reconnect_timer_ = std::jthread([this, expected, ms](std::stop_token st) {
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock<std::mutex> l(m);
    if (!cv.wait_for(l, st, std::chrono::milliseconds(ms), [] { return false; }) && !st.stop_requested())
      send(new CbxReport(CbxReport::RECONNECT, expected), nullptr);
  });
}

void CbxDropClient::start_handler(const actors::msg::Start *) noexcept
{
  if (!factory_)
  {
    std::cerr << "CbxDropClient: no session factory" << std::endl;
    return;
  }
  send(new CbxReport(CbxReport::RECONNECT, session_no_), nullptr);
}

void CbxDropClient::shutdown_handler(const actors::msg::Shutdown *) noexcept
{
  stopping_ = true;
  reconnect_timer_ = {};
  if (session_ && logged_on_)
    session_->logout();
  session_.reset();
  events_.reset();
  logged_on_ = false;
}

void CbxDropClient::report_handler(const CbxReport *m) noexcept
{
  if (m->kind == CbxReport::RECONNECT)
  {
    if (!stopping_ && !rejected_ && m->session == session_no_ && !session_)
      connect();
    return;
  }
  if (m->session != session_no_ || !session_)
    return;
  switch (m->kind)
  {
  case CbxReport::LOGGED_ON:
    logged_on_ = true;
    ++stats_.logons;
    std::cerr << "CbxDropClient: logged on (" << ledger_.live() << " trades held)" << std::endl;
    return;
  case CbxReport::LOGON_REJECTED:
    std::cerr << "CbxDropClient: logon rejected (" << m->text << "): stopped" << std::endl;
    rejected_ = true;
    session_.reset();
    return;
  case CbxReport::DISCONNECTED:
    ++stats_.disconnects;
    logged_on_ = false;
    std::cerr << "CbxDropClient: disconnected (" << m->text << "); reconnecting" << std::endl;
    session_.reset();
    schedule_reconnect();
    return;
  case CbxReport::MESSAGE:
    on_report(m->fix);
    return;
  default:
    return;
  }
}

void CbxDropClient::on_report(const FixMsg &m) noexcept
{
  ++stats_.reports;
  std::string why;
  auto t = to_trade_capture(m, cfg_, why);
  if (!t)
  {
    if (why.rfind("order event", 0) == 0)
      ++stats_.order_events;
    else
    {
      ++stats_.unparsed;
      std::cerr << "CbxDropClient: report not used (" << why << "): " << m.str() << std::endl;
    }
    return;
  }
  ++stats_.trades;
  auto r = ledger_.apply(*t);
  switch (r.what)
  {
  case post_trade::Applied::Duplicate: ++stats_.duplicates; break;
  case post_trade::Applied::Stale: ++stats_.stale; break;
  case post_trade::Applied::Orphan: ++stats_.orphans; break;
  case post_trade::Applied::Invalid:
    ++stats_.invalid;
    std::cerr << "CbxDropClient: report does not fit the ledger: " << m.str() << std::endl;
    break;
  default: break;
  }
  if (r.emit() && subscriber_)
  {
    subscriber_->send(new pt_msg::TradeCapture(r.out), this);
    ++stats_.emitted;
  }
}
}

actors::Actor *create_CbxDropClient(const coinbase_exchange::pt::DropConfig &cfg, actors::Actor *subscriber,
                                    coinbase_exchange::oe::SessionFactory factory)
{
  if (!factory)
    factory = coinbase_exchange::oe::quickfix_session_factory();
  if (!factory)
  {
    std::cerr << "CbxDropClient: not created: no session factory (built without QuickFIX?)" << std::endl;
    return nullptr;
  }
  if (cfg.products.empty())
  {
    std::cerr << "CbxDropClient: not created: no products" << std::endl;
    return nullptr;
  }
  return new coinbase_exchange::pt::CbxDropClient(cfg, std::move(factory), subscriber);
}

actors::Actor *create_CbxDropClient(const std::string &config_path, actors::Actor *subscriber)
{
  coinbase_exchange::pt::DropConfig cfg;
  std::string err;
  if (!coinbase_exchange::pt::DropConfig::from_file(config_path, cfg, err))
  {
    std::cerr << "CbxDropClient: not created, cannot read " << config_path << ": " << err << std::endl;
    return nullptr;
  }
  return create_CbxDropClient(cfg, subscriber);
}
