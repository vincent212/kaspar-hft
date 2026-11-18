#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Kraken fills for post-trade: the mapping of one Kraken fill to
// post_trade::msg::TradeCapture, the configuration, and the session
// interface (feed.md).
//
// Sources:
//   spot     WebSocket v2 `executions` (exec_type = trade), with snap_trades
//            (the last 50) at every subscription; and REST
//            POST /0/private/TradesHistory for reconciliation. The WebSocket
//            exec_id and the REST trade txid are the same identifier
//            ("TGBB7L-HT5LX-J3BZ4A"): the ledger key. PRODUCTION ONLY; never
//            run by the agent.
//   futures  GET /derivatives/api/v3/fills (the last 100) polled as the
//            stream, and the same endpoint paged back with lastFillTime for
//            reconciliation; fill_id is the key. Demo environment.
// Kraken sends no amendment, cancellation or bust of a fill through these
// APIs: every report is Event::New. trade_date and trade_time_ns are UTC
// (Kraken has no trading day; feed.md, "Trading day").

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "enum/e_names.hpp"
#include "kraken_oe/KrakenConfig.hpp"
#include "kraken_oe/KrakenSessions.hpp"
#include "post_trade/msg/TradeCapture.hpp"

namespace kraken::pt
{
  enum class Source
  {
    SpotWs,      // executions channel
    SpotRest,    // TradesHistory
    FuturesPoll, // GET /fills, newest 100
    FuturesRest, // GET /fills?lastFillTime=..., reconciliation
  };
  inline bool from_rest(Source s) { return s == Source::SpotRest || s == Source::FuturesRest; }

  struct PtInstrument
  {
    std::string symbol;    // WebSocket / futures symbol: BTC/USD, PF_XBTUSD
    std::string rest_pair; // spot TradesHistory `pair`: XXBTZUSD ("" for futures)
    std::string asset;     // Kaspar asset name; "" if not mapped
    std::string lot;       // quantity unit of TradeCapture.qty, decimal text: "0.00000001"
    en::x venue = en::x::KRAKEN;
    int sym = -1;
  };

  struct FillsConfig
  {
    oe::Endpoint ep;
    std::string key_file;
    int poll_ms = 1000;              // futures: /fills polling period
    int reconcile_every_s = 60;      // 0: only at logon
    int reconcile_lookback_s = 3600; // REST start = now - lookback
    int reconnect_ms = 2000;
    std::vector<PtInstrument> instruments;
    bool sim = false;

    static bool from_file(const std::string &path, FillsConfig &out, std::string &err);
    bool valid(std::string &err) const;
    // By WebSocket/futures symbol or by REST pair.
    const PtInstrument *find(const std::string &symbol_or_pair) const;
  };

  // One fill. A REST TradesHistory row carries its txid outside the object:
  // pass it as `key` (ignored for the other sources). false with the reason
  // when it is not a fill or does not map.
  bool to_trade_capture(const nlohmann::json &f, const std::string &key, Source src, const FillsConfig &cfg,
                        post_trade::msg::TradeCapture &out, std::string &err);

  // "63123.45" -> (6312345, 2); false if not a plain decimal.
  bool parse_decimal(const std::string &s, int64_t &mantissa, int &decimals);

  struct FillsEvents
  {
    virtual ~FillsEvents() = default;
    virtual void logged_in() = 0;
    virtual void login_failed(const std::string &why) = 0;
    virtual void disconnected(const std::string &why) = 0;
    virtual void fill(const nlohmann::json &f, const std::string &key, Source src) = 0;
    // A reconciliation pass ended: rows read, ok = no error.
    virtual void reconciled(size_t rows, bool ok, const std::string &err) = 0;
  };

  class FillsSession
  {
  public:
    virtual ~FillsSession() = default;
    virtual void logon() = 0;
    // REST fills from start_ns to now; rows come as fill(..., from REST), then
    // reconciled(). false if not logged in.
    virtual bool reconcile(uint64_t start_ns) = 0;
    virtual void logout() = 0;
  };

  using FillsSessionFactory = std::function<std::unique_ptr<FillsSession>(FillsEvents &)>;

  // Kraken Futures (demo when ep.testnet): refuses any other host. Polls
  // GET /fills; fills older than since_ns (default: now) are history and not
  // reported by the poll (reconciliation still reads them).
  FillsSessionFactory futures_fills_factory(const FillsConfig &cfg, oe::HttpFn http = oe::real_http(),
                                            uint64_t since_ns = 0);
  // Kraken spot (production only; never run by the agent).
  FillsSessionFactory spot_fills_factory(const FillsConfig &cfg, oe::HttpFn http = oe::real_http());
}
