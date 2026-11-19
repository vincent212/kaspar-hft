/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "bybit_post_trade/act/BybitFillsClient.hpp"

#include "bybit_post_trade/if/BybitFillsClient.hpp"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "bybit_oe/BybitPrivate.hpp"
#include "bybit_post_trade/SimFillSource.hpp"
#include "logger/act/Logger.hpp"

namespace bybit_pt
{
using msg::FillEvent;

namespace
{
  uint64_t now_ms()
  {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());
  }

  // Sleep ms unless stopped; true if the time ran out.
  bool sleep_for(std::stop_token st, int ms)
  {
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock<std::mutex> l(m);
    return !cv.wait_for(l, st, std::chrono::milliseconds(ms), [] { return false; }) && !st.stop_requested();
  }
}

class BybitFillsClient::Forwarder : public FillEvents
{
public:
  Forwarder(BybitFillsClient *c, uint64_t n) : c_(c), n_(n) {}
  void logged_in() override { c_->send(new FillEvent(FillEvent::LOGGED_IN, n_), nullptr); }
  void login_rejected(const std::string &why) override
  {
    auto e = new FillEvent(FillEvent::LOGIN_REJECTED, n_);
    e->text = why;
    c_->send(e, nullptr);
  }
  void disconnected(const std::string &why) override
  {
    auto e = new FillEvent(FillEvent::DISCONNECTED, n_);
    e->text = why;
    c_->send(e, nullptr);
  }
  void execution(const std::string &json, const std::string &category, bool from_rest) override
  {
    auto e = new FillEvent(FillEvent::EXECUTION, n_);
    e->json = json;
    e->category = category;
    e->from_rest = from_rest;
    c_->send(e, nullptr);
  }
  void reconciled(const std::string &category, int pages, int count, bool ok, const std::string &why) override
  {
    auto e = new FillEvent(FillEvent::RECONCILED, n_);
    e->category = category;
    e->pages = pages;
    e->count = count;
    e->ok = ok;
    e->text = why;
    c_->send(e, nullptr);
  }

private:
  BybitFillsClient *c_;
  uint64_t n_;
};

BybitFillsClient::BybitFillsClient(ClientConfig cfg, SourceFactory factory, actors::Actor *subscriber)
    : cfg_(std::move(cfg)), factory_(std::move(factory)), subscriber_(subscriber)
{
  MESSAGE_HANDLER(actors::msg::Start, on_start);
  MESSAGE_HANDLER(actors::msg::Shutdown, on_shutdown);
  MESSAGE_HANDLER(FillEvent, on_event);
}

BybitFillsClient::~BybitFillsClient()
{
  reconnect_timer_ = {};
  ticker_ = {};
  source_.reset();
  fwd_.reset();
}

void BybitFillsClient::on_start(const actors::msg::Start *) noexcept
{
  connect();
  if (cfg_.reconcile_every_s > 0)
  {
    const int ms = cfg_.reconcile_every_s * 1000;
    ticker_ = std::jthread([this, ms](std::stop_token st) {
      while (sleep_for(st, ms))
        send(new FillEvent(FillEvent::RECONCILE, 0), nullptr);
    });
  }
}

void BybitFillsClient::on_shutdown(const actors::msg::Shutdown *) noexcept
{
  stopping_ = true;
  reconnect_timer_ = {};
  ticker_ = {};
  if (source_)
    source_->logout();
  source_.reset();
  ready_ = false;
  log_omg("BybitFillsClient: %lu executions, %lu captured, %lu duplicates, %lu reconciliations (%lu failed)",
          stats_.executions, stats_.emitted, stats_.duplicates, stats_.reconciliations,
          stats_.reconcile_failures);
}

void BybitFillsClient::connect() noexcept
{
  ++source_no_;
  source_.reset();
  fwd_ = std::make_unique<Forwarder>(this, source_no_);
  try
  {
    source_ = factory_(cfg_.source, *fwd_);
    log_omg("Bybit fills %lu: logging in to %s", source_no_, cfg_.source.ws_host);
    source_->logon();
  }
  catch (const std::exception &e)
  {
    source_.reset();
    log_err("cannot create the Bybit fills source: %s", e.what());
    schedule_reconnect();
  }
}

void BybitFillsClient::schedule_reconnect() noexcept
{
  if (stopping_)
    return;
  const auto expected = source_no_;
  const int ms = cfg_.reconnect_ms;
  reconnect_timer_ = std::jthread([this, expected, ms](std::stop_token st) {
    if (sleep_for(st, ms))
      send(new FillEvent(FillEvent::RECONNECT, expected), nullptr);
  });
}

void BybitFillsClient::reconcile_all() noexcept
{
  if (!source_ || !ready_ || pending_ > 0)
    return;
  uint64_t from = reconciled_to_ms_;
  if (from == 0)
    from = cfg_.since_ms ? cfg_.since_ms : now_ms() / 86400000 * 86400000;
  else
    from = from > uint64_t(cfg_.margin_s) * 1000 ? from - uint64_t(cfg_.margin_s) * 1000 : 0;
  const uint64_t to = now_ms();
  std::set<std::string> cats;
  for (const auto &[k, i] : cfg_.instruments)
    cats.insert(i.category);
  pending_ = int(cats.size());
  pending_ok_ = true;
  pending_to_ = to;
  for (const auto &c : cats)
  {
    // Bybit answers at most 7 days per request.
    for (uint64_t a = from; a < to; a += 7ull * 86400000)
    {
      const uint64_t b = std::min(to, a + 7ull * 86400000 - 1);
      if (b < to)
        ++pending_; // one more reconciled() for this extra window
      source_->reconcile(c, a, b);
    }
  }
}

void BybitFillsClient::on_event(const FillEvent *m) noexcept
{
  if (m->kind == FillEvent::RECONCILE)
    return reconcile_all();
  if (m->source != source_no_)
    return; // from a source already replaced
  switch (m->kind)
  {
  case FillEvent::LOGGED_IN:
    ready_ = true;
    ++stats_.logins;
    log_omg("Bybit fills %lu: logged in; reconciling", source_no_);
    pending_ = 0;
    reconcile_all();
    break;
  case FillEvent::LOGIN_REJECTED:
    ready_ = false;
    log_err("Bybit fills: login rejected: %s; no fills are captured", m->text);
    break;
  case FillEvent::DISCONNECTED:
    ready_ = false;
    ++stats_.disconnects;
    log_err("Bybit fills: disconnected: %s; reconnecting", m->text);
    source_.reset();
    schedule_reconnect();
    break;
  case FillEvent::RECONNECT:
    if (!stopping_)
      connect();
    break;
  case FillEvent::EXECUTION:
    on_execution(*m);
    break;
  case FillEvent::RECONCILED:
    ++stats_.reconciliations;
    stats_.rest_pages += size_t(m->pages);
    if (!m->ok)
    {
      ++stats_.reconcile_failures;
      pending_ok_ = false;
      log_err("Bybit fills: reconciliation of %s failed after %d pages: %s; the window is retried",
              m->category, m->pages, m->text);
    }
    else
      log_inf("Bybit fills: reconciled %s: %d executions in %d pages", m->category, m->count, m->pages);
    if (--pending_ <= 0)
    {
      pending_ = 0;
      if (pending_ok_)
        reconciled_to_ms_ = pending_to_;
    }
    break;
  default:
    break;
  }
}

void BybitFillsClient::on_execution(const FillEvent &m) noexcept
{
  ++stats_.executions;
  ++(m.from_rest ? stats_.from_rest : stats_.from_stream);
  nlohmann::json j;
  try
  {
    j = nlohmann::json::parse(m.json);
  }
  catch (const std::exception &)
  {
    ++stats_.bad;
    log_err("Bybit fills: unparsable execution: %s", m.json);
    return;
  }
  post_trade::msg::TradeCapture tc;
  std::string why;
  switch (to_capture(j, m.category, cfg_.instruments, m.from_rest, tc, why))
  {
  case Parse::Ok:
    break;
  case Parse::NotATrade:
    ++stats_.not_trades;
    log_inf("Bybit fills: %s not captured (%s)", j.value("execId", std::string()), why);
    return;
  case Parse::NoInstrument:
    ++stats_.unmapped;
    log_err("Bybit fills: execution %s in %s: CHECK POSITIONS", j.value("execId", std::string()), why);
    return;
  case Parse::BadQuantity:
  case Parse::Malformed:
    ++stats_.bad;
    log_err("Bybit fills: execution not captured: %s: %s: CHECK POSITIONS", why, m.json);
    return;
  }

  // One ledger per UTC day, and the previous day's.
  post_trade::TradeLedger *led = &ledger_;
  if (date_ == 0)
    date_ = tc.trade_date;
  if (tc.trade_date > date_)
  {
    prev_ = std::move(ledger_);
    ledger_ = post_trade::TradeLedger{};
    prev_date_ = date_;
    date_ = tc.trade_date;
  }
  else if (tc.trade_date < date_)
  {
    if (tc.trade_date == prev_date_ || prev_date_ == 0)
    {
      led = &prev_;
      prev_date_ = tc.trade_date;
    }
    else
    {
      ++stats_.older_than_ledger;
      log_wrn("Bybit fills: execution %s of %u is older than the ledgers (%u, %u): not captured", tc.trade_id,
              tc.trade_date, date_, prev_date_);
      return;
    }
  }
  auto r = led->apply(tc);
  if (r.what == post_trade::Applied::Duplicate)
  {
    ++stats_.duplicates;
    return;
  }
  if (!r.emit())
  {
    log_err("Bybit fills: execution %s: %s", tc.trade_id, post_trade::to_string(r.what));
    return;
  }
  ++stats_.emitted;
  log_fil("Bybit fill %s %s %s %ld @ %.10g order %s link %s%s", tc.trade_id, tc.symbol,
          post_trade::to_string(tc.side), tc.qty, tc.price(), tc.venue_order_id, tc.account,
          m.from_rest ? " (REST)" : "");
  if (subscriber_)
    subscriber_->send(new post_trade::msg::TradeCapture(r.out), this);
}

bool ClientConfig::from_file(const std::string &path, ClientConfig &c, std::string &err)
{
  try
  {
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(path, pt);
    c.sim = pt.get<bool>("fills.sim", false);
    c.source.testnet = pt.get<bool>("fills.testnet", true);
    c.source.rest_host = pt.get<std::string>("fills.rest_host", c.source.rest_host);
    c.source.ws_host = pt.get<std::string>("fills.ws_host", c.source.ws_host);
    c.key_file = pt.get<std::string>("fills.key_file", c.key_file);
    c.reconcile_every_s = pt.get<int>("fills.reconcile_every_s", c.reconcile_every_s);
    c.margin_s = pt.get<int>("fills.margin_s", c.margin_s);
    c.reconnect_ms = pt.get<int>("fills.reconnect_ms", c.reconnect_ms);
    c.sim_config.fill_every_ms = pt.get<int>("fills.sim_fill_every_ms", 0);
    if (auto ins = pt.get_child_optional("instruments"))
      for (const auto &[k, v] : *ins)
      {
        Instrument i;
        std::istringstream ss(v.get_value<std::string>());
        ss >> i.category >> i.symbol >> i.qty_unit;
        if (!ss)
        {
          err = "instrument " + k + ": want '<category> <symbol> <qty_unit>'";
          return false;
        }
        i.asset = std::stoi(k);
        c.instruments[{i.category, i.symbol}] = i;
      }
    if (!c.sim && !bybit::auth::load(c.key_file, c.source.credentials, err))
      return false;
  }
  catch (const std::exception &e)
  {
    err = e.what();
    return false;
  }
  return c.valid(err);
}

bool ClientConfig::valid(std::string &err) const
{
  if (instruments.empty())
  {
    err = "no instruments";
    return false;
  }
  for (const auto &[k, i] : instruments)
    if ((i.category != "spot" && i.category != "linear") || i.symbol.empty() || !(i.qty_unit > 0))
    {
      err = "instrument " + k.first + " " + k.second + ": category spot or linear, symbol and qty_unit required";
      return false;
    }
  if (reconnect_ms <= 0 || margin_s < 0 || reconcile_every_s < 0)
  {
    err = "reconnect_ms must be positive, margin_s and reconcile_every_s not negative";
    return false;
  }
  if (sim)
    return true;
  try
  {
    bybit::priv::guard(source.testnet, source.rest_host, source.ws_host);
  }
  catch (const std::exception &e)
  {
    err = e.what();
    return false;
  }
  if (source.credentials.empty())
  {
    err = "no API key loaded";
    return false;
  }
  return true;
}

}

actors::Actor *create_BybitFillsClient(const bybit_pt::ClientConfig &cfg, actors::Actor *subscriber,
                                       bybit_pt::SourceFactory factory)
{
  std::string err;
  if (!cfg.valid(err))
  {
    std::cerr << "BybitFillsClient: not created, bad config: " << err << std::endl;
    return nullptr;
  }
  if (!factory)
    factory = cfg.sim ? bybit_pt::SimFillSource::make_factory(cfg.sim_config, cfg.instruments)
                      : bybit_pt::rest_ws_source_factory();
  return new bybit_pt::BybitFillsClient(cfg, std::move(factory), subscriber);
}

actors::Actor *create_BybitFillsClient(const std::string &path, actors::Actor *subscriber)
{
  bybit_pt::ClientConfig cfg;
  std::string err;
  if (!bybit_pt::ClientConfig::from_file(path, cfg, err))
  {
    std::cerr << "BybitFillsClient: not created, cannot read " << path << ": " << err << std::endl;
    return nullptr;
  }
  return create_BybitFillsClient(cfg, subscriber);
}
