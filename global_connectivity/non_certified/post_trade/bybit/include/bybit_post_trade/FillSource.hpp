#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// FillSource: where BybitFillsClient's executions come from. NOT CERTIFIED.
//
//   RestWsFillSource  the private WebSocket topic `execution` (live) and
//                     GET /v5/execution/list (reconciliation), on Bybit's
//                     testnet or demo hosts only while testnet = true
//   SimFillSource     an in-process venue for --sim runs
//   FakeFillSource    the test double (tests/)
//
// Events are delivered on the source's own threads; the receiver only
// queues them. reconcile() pages through the executions of one category
// between two times and delivers each as execution(..., from_rest = true),
// then reconciled().

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "bybit_oe/BybitAuth.hpp"

namespace bybit_pt
{
  struct FillEvents
  {
    virtual ~FillEvents() = default;
    virtual void logged_in() = 0;
    virtual void login_rejected(const std::string &why) = 0;
    virtual void disconnected(const std::string &why) = 0;
    // One execution element, as JSON text, unchanged. category: the stream
    // element's own, or the REST request's.
    virtual void execution(const std::string &json, const std::string &category, bool from_rest) = 0;
    // End of one reconcile(): pages read, executions delivered; ok false
    // with the reason when a page failed (the window is retried later).
    virtual void reconciled(const std::string &category, int pages, int count, bool ok,
                            const std::string &why) = 0;
  };

  class FillSource
  {
  public:
    virtual ~FillSource() = default;
    virtual void logon() = 0;
    virtual void reconcile(const std::string &category, uint64_t from_ms, uint64_t to_ms) = 0;
    virtual void logout() = 0;
  };

  struct SourceConfig
  {
    std::string rest_host = "api-testnet.bybit.com";
    std::string ws_host = "stream-testnet.bybit.com";
    bool testnet = true;
    bybit::auth::Credentials credentials;
    int recv_window_ms = 5000;
    int page_pause_ms = 250; // between REST pages: at most 4 requests a second
  };

  using SourceFactory = std::function<std::unique_ptr<FillSource>(const SourceConfig &, FillEvents &)>;

  // Throws if testnet is set and a host is not a Bybit test host.
  SourceFactory rest_ws_source_factory();

  struct SimConfig
  {
    int fill_every_ms = 0; // 0: no generated executions
    uint64_t seed = 1;
  };
}
