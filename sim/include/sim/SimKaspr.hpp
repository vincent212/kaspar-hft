#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "actors/act/Manager.hpp"
#include "actors/act/Group.hpp"
#include "enum/e_names.hpp"
#include "light/if/PCoord.hpp"
#include "light/if/QCoord.hpp"
#include "logger/act/Logger.hpp"

namespace sim
{
  /**
   * Replay simulator: drives a recorded L3 .bin through the same books,
   * lights and order manager the live system uses.
   *
   *   BFA(.bin) -> OB -> light22 (shadow) -> SOM (queue-aware sim fills)
   *                 ^                          |
   *              Timer (market time)        PositionManager
   *
   * Deliberately excluded: Aggregator (removed from this repo), MarketMaker
   * (it simulates a counterparty; we fill against recorded flow), and the
   * ZMQ/registry/coordinator plumbing. See models/PLAN.md D0b.
   */
  struct SlippageProbe;

  struct SimKaspr : public actors::Manager
  {
    SimKaspr(std::string data_file,
             std::string universe_json,
             std::string contract,
             std::string config_dir,
             en::x venue,
             uint64_t ob_debug_from = 0,
             uint64_t end_ts = 0,
             int place_rate_bp = -1,
             uint32_t rng_seed = 0,
             int ord_sz = -1,
             int ob_delay_us = -1,
             int ob_cancel_delay_us = -1,
             int max_dist = -1,
             int nlights_per_side = -1,
             int probe_size = 0,
             std::string probe_out = "",
             std::vector<uint64_t> probe_fires = {});
    ~SimKaspr() override = default;

  private:
    std::string data_file_;
    std::string universe_json_;
    std::string contract_;        // e.g. "ESH5"; empty = every contract of the asset
    std::string config_dir_;
    en::x venue_;
    uint64_t ob_debug_from_;  // 0 = off; else arm OB debug from this epoch (ns)
    uint64_t end_ts_;         // 0 = whole file; else stop replay past this transactTime
    // Per-run light overrides. The experiment sweeps placement rate and size,
    // so these belong on the command line, not in a config file that would have
    // to be edited per grid cell. -1 / 0 means "leave lights.ini alone".
    int      place_rate_bp_;
    uint32_t rng_seed_;
    int      ord_sz_;
    // Modelled one-way wire latency to the matching engine, microseconds.
    // OB holds every sim order on its del_q until ts0 + delay has passed in
    // market time, so this is the single knob that decides how much of the
    // real flow gets in front of us. -1 = leave OB's own default (1000 us).
    int      ob_delay_us_;
    // Cancel latency. Same wire as an order, so -1 (= ob_delay_us) is the
    // physically right default; set it only to test the asymmetric case.
    int      ob_cancel_delay_us_;
    // How deep the light will rest, in ticks from the touch. The binding
    // limit on working size: (max_dist + 1) levels x lev_orders_max.
    // -1 = leave lights.ini alone.
    int      max_dist_;
    // How many lights per side. Production (kaspr.cpp NUM_LIGHTS_PER_SIDE) runs
    // 4 buy + 4 sell per instrument; the sim ran 1 + 1, which is not the same
    // algorithm. A light holds exactly ONE order at one price (ord_info_t is a
    // single slot), so N lights is what lets the shadow rest at N different
    // prices at once and have the market come to it. With one light there is
    // no such thing as resting at several levels, whatever nlevels or max_dist
    // say. -1 = take lights.ini's value.
    int      nlights_per_side_;
    // SlippageProbe: parent size per leg (0 = no probe), CSV path, and the
    // fire times as epoch ns. The times are computed by the caller against a
    // real tz database -- see main.cpp -- because chutil::Time is UTC and a
    // fixed wall-clock hour would drift against ET when DST starts.
    int                   probe_size_;
    std::string           probe_out_;
    std::vector<uint64_t> probe_fires_;
    // The MINIMUM a measurement window runs for, seconds of market time. Not
    // the timer period -- the timer polls every second, because a window also
    // has to wait for both legs to fill their size before it can close.
    int probe_window_s_ = 15 * 60;

    actors::Group* group_ = nullptr;
    SlippageProbe* probe_ = nullptr;
    polonaise::logger::act::Logger* logger_ = nullptr;

    // Dense [venue][asset_id] layout, which is what SOM expects.
    std::vector<std::vector<actor_ptr>> order_books_;
    // Books we actually created, with the asset id each was built for. OB's
    // own sym field is not reliable to read back, so track it alongside.
    std::vector<actor_ptr> books_;
    std::vector<uint> book_syms_;

    actor_ptr timer_ = nullptr;
    actor_ptr som_ = nullptr;
    actor_ptr bfa_ = nullptr;
    actor_ptr position_manager_ = nullptr;
    std::vector<actor_ptr> lights_;
    // Split by side, because that is what the probe targets: BUY lights get
    // +sz and SEL lights get -sz. They share one PCoord; the differing targets
    // are what keep both sides live. See create_lights().
    std::vector<actor_ptr> buy_lights_, sel_lights_;

    std::map<std::string, light::PCoord*> pcoord_map_;
    std::vector<std::string> registered_;   // symbols registered from the universe JSON

    // Load the universe JSON and register every future with RefData, so the
    // securityID -> Asset mapping exists BEFORE any market data arrives.
    // Returns the number of instruments registered.
    size_t load_universe();

    void create_order_books();
    void create_timer();
    void create_som();
    void create_probe();
    void create_lights();
    void create_position_manager();
    void create_bfa();
  };
}
