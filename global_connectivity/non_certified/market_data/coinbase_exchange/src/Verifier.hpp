#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The public live check of cbx_md (testing ladder step 2): what can be
// checked against Coinbase Exchange with no account.
//
//   L2   level2_batch + matches -> CoinbaseExchangeL2Sync -> Mbp2Mbo -> OB.
//        Every REST level-2 book (fetched every 10 s) is compared with the
//        stream's book at the states within +-3 s of the REST book's time:
//        top 1, 10 and 50 levels per side, exact price and size.
//        OB's levels are compared with Mbp2Mbo's after every REST comparison.
//   L3   The REST level-3 book A (sequence S_A), then every public match with
//        S_A < sequence <= S_B applied by CoinbaseExchangeSync (partial-stream
//        mode), compared order by order with the next REST level-3 book B:
//        orders a match touched must be in B at the size predicted, or gone;
//        orders filled by a match must not be in B; the queue order at each
//        level in A must be preserved in B. The sync's records go to a second
//        OB, whose levels are compared with the sync's after every round.

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "coinbase_exchange/CoinbaseExchangeL2Sync.hpp"
#include "coinbase_exchange/CoinbaseExchangeSync.hpp"
#include "coinbase_exchange/act/CoinbaseExchangeFeed.hpp"

namespace coinbase_exchange::verify
{
  struct Totals
  {
    // L2 against REST level 2
    uint64_t l2_rest = 0, l2_rest_compared = 0;
    uint64_t l2_match_top1 = 0, l2_match_top10 = 0, l2_match_top50 = 0;
    uint64_t l2_nearest_top10 = 0;           // the state nearest in time matched top 10
    uint64_t l2_nearest_diff_levels = 0;     // summed over comparisons: differing levels of the top 50
    double l2_offset_ms_sum = 0;             // book time of the matching state minus REST time
    uint64_t l2_crossed_states = 0, l2_states = 0;
    // L2 chain
    uint64_t ob_l2_checks = 0, ob_l2_level_mismatch = 0, ob_l2_bbo_mismatch = 0;
    // L3 against REST level 3
    uint64_t l3_rest = 0, l3_rounds = 0, l3_rest_older = 0;
    uint64_t l3_matches_applied = 0, l3_matches_discarded = 0, l3_unknown_maker = 0, l3_px_mismatch = 0;
    uint64_t l3_touched = 0, l3_touched_ok = 0, l3_touched_gone = 0, l3_touched_bad = 0;
    uint64_t l3_filled = 0, l3_filled_resurrected = 0;
    uint64_t l3_untouched_both = 0, l3_untouched_size_changed = 0;
    uint64_t l3_gone = 0, l3_new = 0;
    uint64_t l3_levels_checked = 0, l3_order_violations = 0, l3_new_before_old = 0, l3_levels_with_new = 0;
    uint64_t l3_crossed_snapshots = 0;
    // L3 chain
    uint64_t ob_l3_checks = 0, ob_l3_level_mismatch = 0, ob_l3_bbo_mismatch = 0;
  };

  /**
   * The exact L3 check for the authenticated `full`/`level3` stream (testing
   * ladder step 3): the running sync, and for each REST level-3 book (sequence
   * S) a second sync seeded from it and given the stream's messages after S
   * from a ring buffer, up to the running sync's sequence; the two books must
   * be equal order by order (id, side, price, size) and level by level.
   */
  class L3Checker : public FeedHandler
  {
  public:
    struct Totals
    {
      uint64_t checks = 0, equal = 0, skipped = 0, orders_compared = 0, order_diffs = 0, level_diffs = 0;
    };
    explicit L3Checker(const ProductSpec &spec, size_t ring = 300000);
    void on_frame(const nlohmann::json &m, uint64_t recv_ns) override;
    void on_rest(int level, const nlohmann::json &body, uint64_t sent_ns, uint64_t recv_ns) override;
    void on_disconnect(uint64_t now_ns) override;
    bool need_snapshot() const override { return sync_.need_snapshot(); }
    std::string status_line() const override;
    nlohmann::json report() const;
    const Totals &totals() const { return t_; }
    const CoinbaseExchangeSync &sync() const { return sync_; }

  private:
    ProductSpec spec_;
    CoinbaseExchangeSync sync_;
    std::deque<std::pair<nlohmann::json, uint64_t>> ring_;
    size_t ring_max_;
    // the level3 channel's schema message of this connection: sent once, so
    // kept outside the ring and replayed into every second sync first
    nlohmann::json l3_schema_;
    Totals t_;
  };

  class Verifier : public FeedHandler
  {
  public:
    // ob_chain: also run the records through OB and compare its levels.
    Verifier(const ProductSpec &spec, double chunk, bool ob_chain);
    ~Verifier() override;

    void on_frame(const nlohmann::json &m, uint64_t recv_ns) override;
    void on_rest(int level, const nlohmann::json &body, uint64_t sent_ns, uint64_t recv_ns) override;
    void on_disconnect(uint64_t now_ns) override;
    bool need_snapshot() const override { return false; }
    std::string status_line() const override;

    void finish(); // finalise what is pending
    const Totals &totals() const { return t_; }
    nlohmann::json report() const;
    const CoinbaseExchangeL2Sync &l2() const { return *l2_; }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ProductSpec spec_;
    Totals t_;
    std::unique_ptr<CoinbaseExchangeL2Sync> l2_;

    void l2_state(uint64_t recv_ns);
    void l2_finalise(uint64_t now_recv, bool all);
    void l3_finalise(int64_t stream_seq, bool force);
    void l3_round(const nlohmann::json &b);
    void seed_l3(const nlohmann::json &body, uint64_t recv_ns);
    void ob_check_l2();
    void ob_check_l3();
  };
}
