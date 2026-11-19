#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <atomic>
#include <cstdint>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/property_tree/ptree.hpp>
#include <nlohmann/json.hpp>

#include "actors/Actor.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/msg/Start.hpp"
#include "bfile/r_l3.hpp"
#include "coinbase_exchange/CoinbaseExchangeL2Sync.hpp"
#include "coinbase_exchange/CoinbaseExchangeSync.hpp"
#include "coinbase_exchange/Rest.hpp"

namespace coinbase_exchange
{
  /**
   * What the feed actor drives: one product's book from the frames of one
   * WebSocket connection and the REST snapshots the actor fetches for it.
   * L3Handler and L2Handler send Kaspar records to OB; cbx_md's verifier is
   * another implementation.
   */
  struct FeedHandler
  {
    virtual ~FeedHandler() = default;
    virtual void on_frame(const nlohmann::json &m, uint64_t recv_ns) = 0;
    // A REST book fetched for this handler (level 2 or 3) or the product object
    // (level 0), with the times the request was sent and the answer received.
    virtual void on_rest(int level, const nlohmann::json &body, uint64_t sent_ns, uint64_t recv_ns) = 0;
    virtual void on_disconnect(uint64_t now_ns) = 0;
    // true while a level-3 snapshot is needed to (re)synchronise
    virtual bool need_snapshot() const = 0;
    // one line for the --dump monitor
    virtual std::string status_line() const = 0;
  };

  // The production handlers: the L3 sync (full or level3 channel, needs a key)
  // or the L2 fallback (level2_batch + matches, no key), records to `emit`.
  std::unique_ptr<FeedHandler> make_l3_handler(const CoinbaseExchangeSync::Config &cfg,
                                               CoinbaseExchangeSync::Emit emit);
  std::unique_ptr<FeedHandler> make_l2_handler(const ProductSpec &spec, double chunk, size_t depth,
                                               std::function<void(const bfile::l3_t &)> emit);

  /**
   * CoinbaseExchangeFeed -- owns the WebSocket connection to Coinbase Exchange
   * for one product, the REST fetches, and the recording of raw frames.
   *
   *   channels   L3: `full` (or `level3`) + `heartbeat`, signed with the API key
   *              (needs credentials); L2: `level2_batch` + `matches` + `heartbeat`
   *   snapshot   L3: GET /products/<id>/book?level=3 on a helper thread while the
   *              stream is buffered; retried with backoff when the sync says the
   *              snapshot was too old (feed.md section 5)
   *   reconnect  after any error, 1 s later; the handler is told first
   *   record     every frame as "<recv_ns>\t<frame>" to <record_dir>/ws.txt and
   *              every REST answer as "<sent_ns>\t<recv_ns>\t<product>\t<level>\t<body>"
   *              to <record_dir>/rest.txt
   *   guard      refuses to connect if the configured environment and the host
   *              (or the credentials' environment) disagree: a sandbox
   *              configuration never reaches a production host
   *
   * The read loop runs on the actor's thread, as the sim/ crypto feeds do.
   */
  class CoinbaseExchangeFeed : public actors::Actor
  {
  public:
    enum class Mode
    {
      L3Full,   // `full` channel (authenticated)
      L3Level3, // `level3` channel (authenticated, compact)
      L2Public, // `level2_batch` + `matches` (no key)
    };

    struct Config
    {
      std::string product;
      Mode mode = Mode::L2Public;
      Environment environment = Environment::Production; // public data defaults to production
      std::string ws_host;            // empty: from the environment (direct feed when signed)
      std::string rest_host;          // empty: from the environment
      std::string key_file;           // credentials; required for the L3 modes
      std::string record_dir;         // empty: no recording
      bool dump = false;              // print status_line() once a second
      // extra REST fetches for verification: {level, period_s}
      std::vector<std::pair<int, int>> periodic_rest;
      int snapshot_backoff_s = 2;     // first retry after a snapshot that was too old; doubles to 60
      int run_seconds = 0;            // stop after this long (0: until shutdown)

      // [coinbase_exchange] section of an ini file (config/coinbase_exchange.ini)
      static bool from_ptree(const boost::property_tree::ptree &pt, Config &out, std::string &err);
    };

    CoinbaseExchangeFeed(const Config &cfg, std::unique_ptr<FeedHandler> handler);
    ~CoinbaseExchangeFeed() override;

    const char *get_name() const override { return name; }
    bool finished() const { return finished_; }
    FeedHandler &handler() { return *handler_; }
    // false with the reason if the configuration would reach the wrong host
    bool check(std::string &err) const;

  private:
    Config cfg_;
    std::unique_ptr<FeedHandler> handler_;
    std::optional<Credentials> creds_;
    std::string ws_host_, rest_host_;
    std::ofstream ws_rec_, rest_rec_;
    uint64_t last_dump_ns_ = 0;
    uint64_t started_ns_ = 0;
    std::atomic<bool> finished_{false};
    int backoff_s_ = 0;
    uint64_t next_snapshot_ns_ = 0;
    std::vector<uint64_t> next_periodic_ns_;

    // REST fetches run on a helper thread, one at a time; answers come back here.
    struct RestJob
    {
      int level = 0;
      uint64_t sent_ns = 0, recv_ns = 0;
      std::string body, error;
    };
    std::mutex rest_mu_;
    std::deque<RestJob> rest_done_;
    std::atomic<bool> rest_busy_{false};
    std::thread rest_thread_;

    void start_handler(const actors::msg::Start *) noexcept;
    void continue_handler(const actors::msg::Continue *) noexcept;
    void run_connection();
    void poll_rest(uint64_t now);
    void start_rest(int level);
    std::string subscribe_message() const;
    void record_rest(const RestJob &j);
  };
}

// Feed into `ob` (an OB actor) for the configured mode, or nullptr with the
// reason on stderr for a bad configuration.
actors::Actor *create_CoinbaseExchangeFeed(const coinbase_exchange::CoinbaseExchangeFeed::Config &cfg,
                                           const coinbase_exchange::ProductSpec &spec, actors::Actor *ob,
                                           double l2_chunk = 0.0001, size_t l2_depth = 1000);
