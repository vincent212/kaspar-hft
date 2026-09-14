/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "sim/SimKaspr.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/info_parser.hpp>

#include "chutil/Assert.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/mtim/act/Timer.hpp"
#include "frame/mda/act/BFA.hpp"
#include "frame/ob/if/OB.hpp"
#include "som/if/SOM.hpp"
#include "light/if/light22.hpp"
#include "positionman/if/PositionManager.hpp"
#include "sim/act/SlippageProbe.hpp"

namespace sim
{

SimKaspr::SimKaspr(std::string data_file,
                   std::string universe_json,
                   std::string contract,
                   std::string config_dir,
                   en::x venue,
                   uint64_t ob_debug_from,
                   uint64_t end_ts,
                   int place_rate_bp,
                   uint32_t rng_seed,
                   int ord_sz,
                   int ob_delay_us,
                   int ob_cancel_delay_us,
                   int ob_feed_delay_us,
                   int max_dist,
                   int nlights_per_side,
                   int probe_size,
                   std::string probe_out,
                   std::vector<uint64_t> probe_fires)
    : Manager("sim_manager")
    , data_file_(std::move(data_file))
    , universe_json_(std::move(universe_json))
    , contract_(std::move(contract))
    , config_dir_(std::move(config_dir))
    , venue_(venue)
    , ob_debug_from_(ob_debug_from)
    , end_ts_(end_ts)
    , place_rate_bp_(place_rate_bp)
    , rng_seed_(rng_seed)
    , ord_sz_(ord_sz)
    , ob_delay_us_(ob_delay_us)
    , ob_cancel_delay_us_(ob_cancel_delay_us)
    , ob_feed_delay_us_(ob_feed_delay_us)
    , max_dist_(max_dist), nlights_per_side_(nlights_per_side)
    , probe_size_(probe_size)
    , probe_out_(std::move(probe_out))
    , probe_fires_(std::move(probe_fires))
    , probe_window_s_(probe_fires_.size() > 1
                          ? int((probe_fires_[1] - probe_fires_[0]) / 1000000000ull)
                          : 15 * 60)
{
  std::cerr << "SimKaspr: data=" << data_file_ << "\n"
            << "          universe=" << universe_json_ << "\n"
            << "          contract=" << (contract_.empty() ? "<all ES>" : contract_) << "\n"
            << "          venue=" << en::to_string(venue_) << std::endl;

  logger_ = new polonaise::logger::act::Logger("sim_log", nullptr);
  add_to_manage_q(logger_);

  group_ = new actors::Group("sim_group");

  // Order matters: the universe must be in RefData before books are created,
  // and BFA must be added last so nothing receives data before it is ready.
  auto n = load_universe();
  ASSERTF(n > 0, boost::format("no instruments loaded from %s") % universe_json_);

  create_order_books();
  create_timer();
  logger_->set_timer(timer_);
  // Before the SOM: the probe is its fill subscriber. After the lights: the
  // probe drives them, and they do not exist yet.
  create_probe();
  create_som();
  create_lights();
  if (probe_ && !lights_.empty())
    // The probe drives the two position books directly; it sends no targets.
    probe_->set_lights(buy_lights_, sel_lights_);
    probe_->set_coords(probe_pcoord_buy_, probe_pcoord_sel_);
  create_position_manager();
  create_bfa();

  add_to_manage_q(group_);
}

/**
 * Register every FDF in the universe JSON with RefData.
 *
 * This is what makes the securityID -> Asset mapping exist up front, rather
 * than depending on the instrument definition happening to fall inside the
 * captured window (for the ES front month it does not).
 * RefData::add_future_asset is idempotent, so re-registering a symbol
 * already present is harmless.
 *
 * Tick size: CME ships minPriceIncrement in native units (25) and a
 * dispFactor (0.01); the real tick is their product (0.25). BFA does the same
 * multiplication when it creates assets from an in-stream FDF.
 */
size_t SimKaspr::load_universe()
{
  boost::property_tree::ptree pt;
  try {
    boost::property_tree::read_json(universe_json_, pt);
  } catch (const std::exception& e) {
    ERRF(boost::format("cannot read universe %s: %s") % universe_json_ % e.what());
  }

  // RefData is lazily constructed from a universe CSV and every accessor
  // dereferences it, so it must exist before add_future_asset() is called.
  // Seed it with an EMPTY universe on purpose: add_future_asset() returns the
  // existing Asset untouched when the symbol is already present, so a
  // pre-seeded CSV row would win and leave sec_id unset — exactly the
  // securityID hole we are here to close. Every instrument therefore comes
  // from the JSON, with its real securityID and tick.
  {
    const std::string seed = "sim_universe_seed.csv";
    std::ofstream f(seed);
    ASSERTF(f.is_open(), boost::format("cannot write %s") % seed);
    f << "# generated by sim; instruments are registered from " << universe_json_ << "\n";
    f.close();
    frame::ref::RefData::set_universe(seed);
    frame::ref::RefData::inst();   // force construction
  }

  auto insts = pt.get_child_optional("instruments");
  if (!insts) ERRF(boost::format("universe %s has no 'instruments' key") % universe_json_);

  size_t n = 0;
  for (const auto& kv : *insts) {
    const auto& inst = kv.second;
    if (inst.get<std::string>("type", "") != "FDF") continue;   // futures only

    const auto sec_id = inst.get<int32_t>("securityID", 0);
    const auto sym    = inst.get<std::string>("symbol", "");
    if (sec_id == 0 || sym.empty()) continue;

    const auto cfi   = inst.get<std::string>("cfiCode", "");
    const auto group = inst.get<std::string>("securityGroup", "");
    const auto mpi   = inst.get<double>("minPriceIncrement", 0.0);
    const auto df    = inst.get<double>("dispFactor", 1.0);

    const auto venue_str = inst.get<std::string>("venue", "");
    const en::x venue = venue_str == "CMEMDFUT" ? en::x::CMEMDFUT
                      : venue_str == "CMEMD"    ? en::x::CMEMD
                                                : venue_;

    // Tick size in the units the DATA uses, which is native: the .bin carries
    // securityID-native prices (ESH5 at 5884.00 appears as 588400) and OB
    // hands mbo.pxd straight to ref::Price, whose tick index is pxd / units.
    // So units must be minPriceIncrement itself (25), NOT mpi * dispFactor
    // (0.25) -- that product is the tick of the DISPLAY price and belongs on a
    // path that has already applied dispFactor.
    //
    // Getting this wrong is silent and total: with units = 0.25 every ES
    // record converts to ~2.35M ticks against a 25,592-tick ladder, so the
    // price guard drops all of them, no book is ever built, no EndOfBurst is
    // published, and the run completes reporting nothing. It is consistent
    // with maxpx below, which is hi / mpi on the same native convention.
    (void)df;
    auto* a = frame::ref::RefData::add_future_asset(sym, venue, sec_id, cfi, group, mpi);

    // add_future_asset leaves maxpx at -1, its "unset" sentinel. OB sizes its
    // price ladder from maxpx and asserts maxpx > 1, so it must be real before
    // any book is built. Use the instrument's daily high limit, which
    // build_universe lifts out of the l3_lim records in the capture
    // (high_limit_px is in native units, so ticks = high / minPriceIncrement).
    //
    // Do NOT fall back to BFA's int(180/unit): that is a treasury constant and
    // would cap the ES ladder at 180 points, roughly a third of where ES
    // actually trades. Fail loudly instead — a missing limit means the universe
    // is incomplete, and a silently truncated ladder produces wrong fills
    // rather than an error.
    // Not every instrument has usable limits — inter-commodity spreads
    // (ECES...) ship the sentinel. Set what we can here and enforce the
    // requirement only for contracts we actually build a book for, since that
    // is the only place the ladder is needed.
    if (a) {
      const auto hi = inst.get<double>("high_limit_px", 0.0);
      if (hi > 0.0 && mpi > 0.0) a->maxpx = static_cast<int>(hi / mpi);
    }
    registered_.push_back(sym);
    ++n;
  }
  std::cerr << "SimKaspr: registered " << n << " futures from the universe" << std::endl;
  return n;
}

void SimKaspr::create_order_books()
{
  const auto num_venues = en::x_num_syms();
  // Size by the highest asset id actually allocated, not num_assets() — the
  // seed CSV is empty, so num_assets() does not describe the dynamic ids.
  size_t max_id = 0;
  for (const auto& sym : registered_)
    if (auto a = frame::ref::RefData::get_asset(sym)) max_id = std::max(max_id, static_cast<size_t>(a->id));
  order_books_.resize(num_venues);
  for (auto& row : order_books_) row.resize(max_id + 1, nullptr);

  boost::property_tree::ptree pt_model;

  // Assets come from add_future_asset(), which allocates ids from its own
  // counter — they are NOT a dense 1..num_assets() range seeded by the CSV, so
  // scanning by id finds nothing. Resolve by name instead: the contract when
  // one is named, otherwise every future we registered.
  std::vector<const frame::ref::Asset*> wanted;
  if (!contract_.empty()) {
    auto a = frame::ref::RefData::get_asset(contract_);
    ASSERTF(a, boost::format("contract %s is not in %s") % contract_ % universe_json_);
    wanted.push_back(a);
  } else {
    for (const auto& sym : registered_) {
      if (auto a = frame::ref::RefData::get_asset(sym)) wanted.push_back(a);
    }
  }

  for (auto a : wanted) {
    const auto j = static_cast<size_t>(a->id);
    ASSERTF(a->maxpx > 1,
            boost::format("%s (secID %d) has no usable daily price limit, so its "
                          "price ladder cannot be sized; regenerate the universe "
                          "so l3_lim records are picked up")
              % a->name % a->sec_id);
    ASSERTF(j < order_books_[venue_].size(),
            boost::format("asset id %d for %s exceeds the book table (%zu)")
              % a->id % a->name % order_books_[venue_].size());

    auto ob = new frame::ob::act::OB(nullptr, true /*cross_check*/, this, j, pt_model);
    group_->add(ob);
    order_books_[venue_][j] = ob;
    books_.push_back(ob);
    book_syms_.push_back(j);
    // Arm the book's own tracing. A non-zero epoch defers it (OB.cpp turns
    // debug on once txtim passes it), which is how you narrow in on a cross
    // without tracing the whole session.
    if (ob_debug_from_) ob_set_debug(ob, ob_debug_from_);
    // Wire latency. OB defaults to 1000 us, which is an order of magnitude
    // slower than a colocated CME round trip and so is pessimistic about how
    // much flow gets in front of us -- set it per run rather than inherit it.
    // One call, because the cancel latency is constrained by the order latency
    // and OB checks them together.
    if (ob_delay_us_ >= 0 || ob_cancel_delay_us_ >= 0)
      ob_set_delay(ob, ob_delay_us_ >= 0 ? ob_delay_us_ : 1000, ob_cancel_delay_us_);
    // Inbound leg. Set unconditionally -- 0 is a meaningful value (publish
    // immediately) and is the default, so there is no "leave OB alone" case.
    ob_set_feed_delay(ob, ob_feed_delay_us_);

    std::cerr << "SimKaspr: OB " << a->name << " assetID=" << j
              << " secID=" << a->sec_id
              << (ob_debug_from_ ? " [debug armed]" : "")
              << " delay=" << (ob_delay_us_ >= 0 ? std::to_string(ob_delay_us_)
                                                 : std::string("default")) << "us"
              << " canc_delay=" << (ob_cancel_delay_us_ >= 0
                                      ? std::to_string(ob_cancel_delay_us_) + "us"
                                      : std::string("same"))
              // The inbound leg belongs in the provenance line too. Without it a
              // cell's log cannot say which feed-delay arm produced it, and the
              // two legs of the round trip are set independently.
              << " feed_delay=" << ob_feed_delay_us_ << "us"
              << std::endl;
  }
  ASSERT(!books_.empty(), "no order books created - check --contract and the universe");
}

void SimKaspr::create_timer()
{
  // Market time comes from the book: the Timer advances on the book's
  // end-of-burst, so every alarm is in data time, never wall-clock.
  timer_ = new frame::mtim::act::Timer(books_.front());
  group_->add(timer_);
}

void SimKaspr::create_probe()
{
  if (probe_size_ <= 0 || probe_fires_.empty())
    return;

  // The probe measures one instrument against one mid. With more than one book
  // there is no single "the contract", so require --contract rather than pick
  // one silently.
  if (books_.size() != 1)
  {
    std::cerr << "SimKaspr: --probe-size needs exactly one book; pass --contract "
              << "(have " << books_.size() << ")" << std::endl;
    return;
  }

  auto a = frame::ref::RefData::inst().get_asset(book_syms_[0]);
  ASSERT(a, "probe: no asset for the book");

  SlippageProbe::Config cfg;
  cfg.sym_name  = a->name;
  cfg.sym       = book_syms_[0];
  cfg.parent_sz = probe_size_;
  // The schedule collapses to its bounds: the repeating timer supplies every
  // boundary in between, so the probe needs only where the session starts and
  // where it ends.
  cfg.session_start = probe_fires_.front();
  cfg.session_end   = probe_fires_.back();
  cfg.tick_s        = 1;               // poll the clock every second of market time
  cfg.min_window_ns = uint64_t(probe_window_s_) * 1000000000ull;
  cfg.out_path  = probe_out_;

  probe_ = new SlippageProbe(books_[0], timer_, cfg);
  group_->add(probe_);

  std::cerr << "SimKaspr: probe on " << cfg.sym_name << " parent_sz=" << probe_size_
            << " window=" << probe_window_s_ << "s"
            << (probe_out_.empty() ? "" : (" out=" + probe_out_)) << std::endl;
}

void SimKaspr::create_som()
{
  boost::property_tree::ptree pt_som;
  boost::property_tree::read_info(config_dir_ + "/som.ini", pt_som);
  som_ = create_SOM(venue_, nullptr, pt_som, order_books_, true /*sim_mode*/,
                    false /*spin*/, false /*reset_positions*/,
                    probe_ /*fill_subscriber*/);
  group_->add(som_);
}

void SimKaspr::create_lights()
{
  boost::property_tree::ptree pt_light;
  boost::property_tree::read_info(config_dir_ + "/lights.ini", pt_light);

  // CLI overrides win over the config file, so a sweep is a matter of arguments
  // rather than editing lights.ini for every cell of the grid.
  if (place_rate_bp_ >= 0) pt_light.put("place_rate_bp", place_rate_bp_);
  if (rng_seed_)           pt_light.put("rng_seed", rng_seed_);
  if (ord_sz_ > 0)         pt_light.put("ord_sz", ord_sz_);
  if (max_dist_ > 0)       pt_light.put("max_dist", max_dist_);
  if (nlights_per_side_ > 0) pt_light.put("nlights_per_side", nlights_per_side_);

  // Default 4, to match production (kaspr.cpp NUM_LIGHTS_PER_SIDE). The sim
  // used to hardcode 1 per side, which makes it a different algorithm from the
  // one that runs: a light holds exactly one order at one price, so a single
  // light cannot rest at several levels no matter what nlevels or max_dist are
  // set to, and the "market comes to us" effect that multiple resting prices
  // buy you is simply absent.
  const int nlights = pt_light.get<int>("nlights_per_side", 4);
  ASSERTF(nlights > 0 && nlights <= 64,
          boost::format("nlights_per_side must be 1..64, got %d") % nlights);
  std::cerr << "SimKaspr: lights place_rate_bp=" << pt_light.get<int>("place_rate_bp", 300)
            << " ord_sz=" << pt_light.get<int>("ord_sz", 1)
            << " rng_seed=" << pt_light.get<uint32_t>("rng_seed", 1)
            << " max_dist=" << pt_light.get<int>("max_dist", 4) << std::endl;

  for (size_t i = 0; i < books_.size(); ++i) {
    auto ob  = books_[i];
    auto sym = book_syms_[i];
    auto a   = frame::ref::RefData::inst().get_asset(sym);
    if (!a) continue;

    // ONE PCoord PER SIDE, because targetpos stays 0 and the POSITION is what
    // gives a light work.
    //
    // This is the PositionManager arrangement (PositionManager.hpp:56): nothing
    // ever sends Set(TARGET_POS), and a parent order is executed by telling the
    // PCoord you hold the OPPOSITE position and letting the lights work it back
    // to flat. With targetpos 0, light22.hpp:239,243 read:
    //
    //     BUY works only while pos < 0        (short -> buy it back)
    //     SEL works only while pos > 0        (long  -> sell it down)
    //     pos == 0                             both idle
    //
    // So no SINGLE position value leaves both sides live -- at -sz only the buy
    // side works, at +sz only the sell side, at 0 neither. One shared PCoord can
    // therefore only ever run one leg at a time, which is the sequential design
    // this probe exists to replace. Two books, one per side, is what lets both
    // legs work the same window:
    //
    //     buy-side PCoord   seeded -sz   ->  its BUY lights work it to 0
    //     sel-side PCoord   seeded +sz   ->  its SEL lights work it to 0
    //
    // Attribution stays exact with a single trader id: every light on the buy
    // book is a light22<BUY> and only ever bids, so Fill::side names the book.
    auto pcoord_buy = create_PCoord();
    auto pcoord_sel = create_PCoord();
    pcoord_map_[a->name + "_BUY"] = pcoord_buy;
    pcoord_map_[a->name + "_SEL"] = pcoord_sel;
    probe_pcoord_buy_ = pcoord_buy;
    probe_pcoord_sel_ = pcoord_sel;

    // ONE QCoord PER SIDE, shared by that side's lights -- what
    // SHADOW_ALGORITHM.md has always described and what makes lev_orders_max
    // mean what it says.
    //
    // With a QCoord per light, sz_at_px only ever saw that light's own order,
    // so `lev_orders_max - sz_at_px` capped each light separately: four lights
    // could rest 4 x lev_orders_max at one price, and none of them knew the
    // others were there. The cap read like a per-price limit and was not one.
    //
    // What blocked sharing was the mmid. QCoord::mmid_orders is a uint64_t
    // bitmask with one bit per mmid, and every call site passed mmid 0 -- so
    // all four lights contended for bit 0 and one light's remove_order cleared
    // the flag for its siblings. Giving each light its own mmid fixes that:
    // px_mmid_ord is already keyed px -> mmid -> ord, so distinct mmids give
    // each light its own slot at a price while sz_at_px sums across all of
    // them. 64 bits is ample for the light counts here.
    //
    // Named with the index, exactly as kaspr.cpp does. The name is not
    // cosmetic: light22 mixes it into the per-light RNG seed, so two lights
    // sharing a name would draw the SAME placement stream and act on the same
    // ADDs -- N copies of one light rather than N independent ones.
    auto qcoord_buy = create_QCoord();
    auto qcoord_sel = create_QCoord();

    // The passive bank never crosses -- aggression lives in its own bank below,
    // so that an aggressive arm changes exactly one thing against its control.
    boost::property_tree::ptree pt_passive = pt_light;
    pt_passive.put("aggr_participation_bp", 0);

    for (int i = 0; i < nlights; i++) {
      auto light_buy = create_light22_Shadow_BUY(
          "sim", nullptr, nullptr, "L_" + a->name + "_BUY_" + std::to_string(i),
          en::trader::SIMULATOR,
          a->name, venue_, venue_, qcoord_buy, pcoord_buy, ob, nullptr, 0,
          timer_, som_, i, pt_passive, 0, false);  // mmid = i: its own slot
      group_->add(light_buy);
      lights_.push_back(light_buy);
      buy_lights_.push_back(light_buy);
    }

    for (int i = 0; i < nlights; i++) {
      auto light_sel = create_light22_Shadow_SEL(
          "sim", nullptr, nullptr, "L_" + a->name + "_SEL_" + std::to_string(i),
          en::trader::SIMULATOR,
          a->name, venue_, venue_, qcoord_sel, pcoord_sel, ob, nullptr, 0,
          timer_, som_, i, pt_passive, 0, false);  // mmid = i: its own slot
      group_->add(light_sel);
      lights_.push_back(light_sel);
      sel_lights_.push_back(light_sel);
    }

    // ---- the AGGRESSIVE bank -------------------------------------------
    //
    // One light per side, separate from the passive bank, because aggression
    // riding on the passive lights does not work: a light declines a trade
    // while it already holds a working order, and at any useful place_rate_bp
    // the passive lights are occupied nearly all the time. The 2026-09-14 grid
    // D run asked for 2% of trades and got 2.4%-to-0% of that, ranked inversely
    // with placement rate.
    //
    //   place_rate_bp 0   -- it never shadows an ADD, so every order it sends
    //                        is a cross and attribution is unambiguous; the
    //                        passive bank stays byte-identical to the control.
    //   own QCoord        -- its working orders are coordinated separately, so
    //                        it cannot be throttled by the passive bank's book.
    //   SHARED PCoord     -- position is the one thing the two banks must agree
    //                        on: a cross fills the same parent, and the
    //                        aggressive light must stand down at the target
    //                        like any other.
    //
    // One light is enough because a marketable order clears its slot on arrival
    // rather than resting: at 2% of trades on a ~67 s leg that is a placement
    // every few seconds against a ~500 us occupancy.
    const int aggr_bp = pt_light.get<int>("aggr_participation_bp", 0);
    int n_aggr = 0;
    if (aggr_bp > 0) {
      boost::property_tree::ptree pt_aggr = pt_light;
      pt_aggr.put("place_rate_bp", -1);             // NEVER place passively (0 would mean "every add")
      pt_aggr.put("place_after_n_eob", 0);          // and no deterministic path either
      pt_aggr.put("aggr_participation_bp", aggr_bp);

      // One light holds ONE order (ord_info is a single slot), so a bank of one
      // caps how many crosses can be in flight at once. aggr_nlights_per_side
      // makes that a parameter. One QCoord per bank per side, shared by the
      // bank's lights, exactly as the passive bank does; mmid = i gives each
      // light its own slot in that QCoord's bitmask, and the distinct name
      // gives it its own RNG stream.
      const int n_aggr_side = pt_light.get<int>("aggr_nlights_per_side", 1);
      ASSERTF(n_aggr_side > 0 && n_aggr_side <= 64,
              boost::format("aggr_nlights_per_side must be 1..64, got %d") % n_aggr_side);

      auto qcoord_aggr_buy = create_QCoord();
      auto qcoord_aggr_sel = create_QCoord();

      for (int i = 0; i < n_aggr_side; i++) {
        auto aggr_buy = create_light22_Shadow_BUY(
            "sim", nullptr, nullptr,
            "L_" + a->name + "_AGGR_BUY_" + std::to_string(i),
            en::trader::SIMULATOR,
            a->name, venue_, venue_, qcoord_aggr_buy, pcoord_buy, ob, nullptr, 0,
            timer_, som_, i, pt_aggr, 0, false);
        group_->add(aggr_buy);
        lights_.push_back(aggr_buy);
        buy_lights_.push_back(aggr_buy);
      }

      for (int i = 0; i < n_aggr_side; i++) {
        auto aggr_sel = create_light22_Shadow_SEL(
            "sim", nullptr, nullptr,
            "L_" + a->name + "_AGGR_SEL_" + std::to_string(i),
            en::trader::SIMULATOR,
            a->name, venue_, venue_, qcoord_aggr_sel, pcoord_sel, ob, nullptr, 0,
            timer_, som_, i, pt_aggr, 0, false);
        group_->add(aggr_sel);
        lights_.push_back(aggr_sel);
        sel_lights_.push_back(aggr_sel);
      }

      n_aggr = 2 * n_aggr_side;
    }

    std::cerr << "SimKaspr: " << (2 * nlights + n_aggr) << " lights for " << a->name
              << " -- " << nlights << " passive per side"
              << (n_aggr ? (", + " + std::to_string(n_aggr/2) + " aggressive per side at "
                            + std::to_string(aggr_bp) + "bp") : "")
              << std::endl;
  }
}

void SimKaspr::create_position_manager()
{
  position_manager_ = create_PositionManager("sim", pcoord_map_);
  group_->add(position_manager_);
}

/**
 * BFA keys its book lookup by securityID, so hand it a map built from each
 * Asset's sec_id.
 *
 * NOTE: deliberately does NOT use create_BFA() from interface/mda/if/BFA.hpp.
 * That wrapper converts the dense order_books[venue][asset_id] layout into
 * BFA's sparse map by treating the vector INDEX as the securityID, which only
 * holds when asset_id == securityID. For CME it does not — ESH5 is asset id 1
 * or so and securityID 5002 — and the mismatch silently routes no market data
 * at all ("out of range: unordered_map::at ... sym: 5002").
 */
void SimKaspr::create_bfa()
{
  std::vector<std::unordered_map<int32_t, actor_ptr>> adapter(order_books_.size());
  for (size_t i = 0; i < books_.size(); ++i) {
    auto a = frame::ref::RefData::inst().get_asset(book_syms_[i]);
    if (!a) continue;
    // Key on the ASSET's venue, not the --venue CLI value. BFA routes on the
    // record's own header venue (BFA.hpp:783 `dest_venue = hdr->venue`), so if
    // the two disagree the bucket is empty and NO market data is delivered —
    // the run completes with zero fills and no error, which is the worst way
    // to be wrong.
    const auto a_venue = a->is_exchange_md_set() ? a->get_exchange_md() : venue_;
    ASSERTF(size_t(a_venue.value) < adapter.size(),
            boost::format("venue %d out of range for %s") % int(a_venue.value) % a->name);
    adapter[a_venue][static_cast<int32_t>(a->sec_id)] = books_[i];
    std::cerr << "SimKaspr: BFA route secID=" << a->sec_id << " -> " << a->name << std::endl;
  }

  bfa_ = new frame::mda::act::BFA<false>(adapter, data_file_, this,
                                        frame::mda::act::NoOpFactory{}, false, 0.0,
                                        0, 0, -1, -1, end_ts_);
  group_->add(bfa_);   // last: nothing should receive data before it is ready
}

}
