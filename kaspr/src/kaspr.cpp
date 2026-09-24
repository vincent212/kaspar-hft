/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * kaspr.cpp - Kaspr Trading System for ES futures in simulation mode
 *
 * Copyright 2025 Vincent Maciejewski, & M2 Tech
 */

#include "kaspr.hpp"

#include <iostream>
#include <sstream>
#include <csignal>
#include <thread>
#include <chrono>
#include <cctype>
#include <functional>
#include <fenv.h>
#include <filesystem>

#include "chutil/sig_hand.hpp"

// MQ0 implementation
#include "mq0/act/MQ0_server.hpp"


namespace kaspr {

// Helper to get config directory path
static std::string get_config_dir(const std::string& config_file)
{
    std::filesystem::path p(config_file);
    return p.parent_path().string();
}

// Helper to resolve path relative to config directory
static std::string resolve_path(const std::string& config_dir, const std::string& path)
{
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return path;
    }
    return (std::filesystem::path(config_dir) / path).string();
}

std::vector<int> Kaspr::parse_cpu_list(const std::string& s)
{
    std::vector<int> out;
    std::string tok;
    std::istringstream is(s);
    while (std::getline(is, tok, ',')) {
        // strip whitespace; "16, 48" is the form a human types
        size_t a = tok.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        size_t b = tok.find_last_not_of(" \t");
        tok = tok.substr(a, b - a + 1);
        if (tok.empty()) continue;
        out.push_back(std::stoi(tok));  // throws on garbage, which is correct
    }
    return out;
}

Kaspr::Kaspr(const std::string& config_file, bool reset_positions)
    : Manager("kaspr_manager"), config_file_(config_file), reset_positions_(reset_positions)
{
    std::cerr << "Kaspr: Loading configuration from " << config_file << std::endl;
    if (reset_positions_) {
        std::cerr << "Kaspr: Position reset mode enabled" << std::endl;
    }
    boost::property_tree::read_info(config_file, pt_general);
    config_dir_ = get_config_dir(config_file);

    // Initialize RefData with universe file
    auto universe_file = pt_general.get<std::string>("kaspr.general.universe", "config/universe.csv");
    std::cerr << "Kaspr: Loading universe from " << universe_file << std::endl;
    frame::ref::RefData::set_universe(universe_file);

#ifdef USE_TACHBOOK
    // Read TachBook config (default: disabled)
    enable_tachbook_ = pt_general.get<bool>("kaspr.general.tachbook", false);
    if (enable_tachbook_) {
        std::cerr << "Kaspr: TachBook (MBO L3) enabled - will run silently alongside OB" << std::endl;
    }

    // Latency probe. Default OFF: the production recorder must not grow a
    // TachBook subscriber because a measurement config existed once.
    enable_perf_probe_ = pt_general.get<bool>("kaspr.general.perf_probe", false);
    probe_bin_ms_      = pt_general.get<int>("kaspr.general.perf_bin_ms", 100);
    probe_csv_dir_     = pt_general.get<std::string>("kaspr.general.perf_csv_dir", "");

#ifdef KASPR_VERIFY_TEE
    // Dual-path decode verification tee. Off by default. On, every channel
    // runs its packets through TWO decoders into TWO book sets:
    //   primary = SERIAL  (inline, drives recovery)  -> tach_books   -> _S files
    //   shadow  = PARALLEL(cme_decode_workers)       -> tach_books_v -> _P files
    // This is a differential test harness. Never set it on the production
    // recorder: it doubles the book work and the shadow path is not
    // production-ready.
    //
    // The key is only READ in a VERIFY_TEE build. In the default build the
    // whole block is gone, so a stray cme_verify_parallel true in an ini is
    // inert rather than dangerous -- boost's ptree ignores keys nobody asks
    // for.
    verify_parallel_ = pt_general.get<bool>("kaspr.general.cme_verify_parallel", false);
    if (verify_parallel_)
        std::cerr << "Kaspr: DUAL-PATH VERIFY enabled -- serial primary + "
                     "parallel shadow, two book sets" << std::endl;
#endif
#endif

    // Comma list of cpu ids for the TachBook threads, assigned round-robin in
    // creation order. Default "" = do not pin.
    tachbook_cpus_ = pt_general.get<std::string>("kaspr.general.tachbook_cpus", "");

    // Create Logger first so log_inf works
    logger = new polonaise::logger::act::Logger("kaspr_log", nullptr);
    add_to_manage_q(logger);

    // Create components in order
    create_order_books();
#ifdef USE_TACHBOOK
    create_tach_books();
#ifdef KASPR_VERIFY_TEE
    create_verify_tach_books();  // no-op unless cme_verify_parallel
#endif
    create_probes();      // after the books: nothing to subscribe to before
#endif
    create_support_modules();
    create_som();           // Create SOM before DB so DB can subscribe to it
    create_db();            // DB subscribes to SOM for fills
    create_lights();
    create_positionman();
    create_mtd();
    start_market_data();
}

void Kaspr::create_order_books()
{
    std::cerr << "Kaspr: Creating order books" << std::endl;

    // Resize order_books for all venues
    auto num_venues = en::x_num_syms();
    auto n = frame::ref::RefData::inst().num_assets();
    order_books.resize(num_venues);
    for (size_t i = 0; i < order_books.size(); i++)
        order_books[i].resize(n, nullptr);

    boost::property_tree::ptree pt_model;

    // Create OBs for futures (CMEMDFUT venue) - ES and NQ only
    // Track count per mnemonic for numbered naming (OB_ES1, OB_ES2, etc.)
    std::map<std::string, int> mnem_count;
    std::set<std::string> futures_mnemonics = {"ES"}; // add "NQ" if needed
    for (size_t j = 1; j < n; j++)
    {
        auto a = frame::ref::RefData::inst().get_asset(j);
        if (a && futures_mnemonics.count(a->mnemonic) && a->get_exchange_md() == en::x::CMEMDFUT)
        {
            std::cerr << "Kaspr: Creating futures OB for " << a->name
                      << " (" << a->mnemonic << ", id=" << j << ")" << std::endl;

            auto ob = new frame::ob::act::OB(
                nullptr,  // binrec
                false,    // do_cross_check
                this,     // manager
                j,        // sym
                pt_model  // config
            );

            // Note: OB name is set by constructor based on asset name
            add_to_manage_q(ob);
            order_books[en::x::CMEMDFUT][j] = ob;

            // Sort by mnemonic for aggregation
            if (a->mnemonic == "ES") {
                es_order_books.push_back(ob);
            } else if (a->mnemonic == "NQ") {
                nq_order_books.push_back(ob);
            }

            std::cerr << "Kaspr:   OB name: " << ob->get_name() << std::endl;
        }
    }

    std::cerr << "Kaspr: Created " << es_order_books.size() << " ES order books" << std::endl;
    std::cerr << "Kaspr: Created " << nq_order_books.size() << " NQ order books" << std::endl;
}

#ifdef USE_TACHBOOK
void Kaspr::create_tach_books()
{
    if (!enable_tachbook_) {
        std::cerr << "Kaspr: TachBook disabled (kaspr.general.tachbook = false)" << std::endl;
        return;
    }

    std::cerr << "Kaspr: Creating TachBook (MBO L3) books" << std::endl;

    auto num_venues = en::x_num_syms();
    auto n = frame::ref::RefData::inst().num_assets();
    tach_books.resize(num_venues);
    for (size_t i = 0; i < tach_books.size(); i++)
        tach_books[i].resize(n, nullptr);

    // ZN is here because it is the third liquid book on a different channel
    // (344, CBOT rates) and a different price scale. ES and NQ both arrive on
    // equity channels; measuring only those cannot tell a per-channel effect
    // apart from a per-book one.
    // Round-robin the configured cpus over the books in creation order. This
    // is deliberately dumb: it does not know which book is busy. ESZ6 and the
    // dead M6 books get equal weight, so a short list will land the hot book
    // and an idle one on the same cpu. Order the list with that in mind, or
    // give it as many entries as there are books.
    auto tb_cpu_vec = parse_cpu_list(tachbook_cpus_);
    size_t tb_i = 0;

    std::set<std::string> futures_mnemonics = {"ES", "NQ", "ZN"};
    for (size_t j = 1; j < n; j++)
    {
        auto a = frame::ref::RefData::inst().get_asset(j);
        if (a && futures_mnemonics.count(a->mnemonic) && a->get_exchange_md() == en::x::CMEMDFUT)
        {
            std::cerr << "Kaspr: Creating TachBook for " << a->name
                      << " (" << a->mnemonic << ", id=" << j << ")" << std::endl;

            auto tb = new frame::ob::act::TachBook(j);
            std::set<int> aff;
            if (!tb_cpu_vec.empty()) {
                aff.insert(tb_cpu_vec[tb_i % tb_cpu_vec.size()]);
                std::cerr << "Kaspr:   TachBook " << a->name << " -> cpu "
                          << *aff.begin() << std::endl;
                ++tb_i;
            }
            add_to_manage_q(tb, aff);
            tach_books[en::x::CMEMDFUT][j] = tb;

            if (a->mnemonic == "ES") {
                es_tach_books.push_back(tb);
            } else if (a->mnemonic == "NQ") {
                nq_tach_books.push_back(tb);
            } else if (a->mnemonic == "ZN") {
                zn_tach_books.push_back(tb);
            }
        }
    }

    std::cerr << "Kaspr: Created " << es_tach_books.size() << " ES TachBooks" << std::endl;
    std::cerr << "Kaspr: Created " << nq_tach_books.size() << " NQ TachBooks" << std::endl;
    std::cerr << "Kaspr: Created " << zn_tach_books.size() << " ZN TachBooks" << std::endl;
}

#ifdef KASPR_VERIFY_TEE
// SHADOW TachBook set for the verification tee. Same instruments, same asset
// ids, entirely separate actors. Deliberately a near-copy of
// create_tach_books() rather than a shared helper: the two sets must be able
// to drift apart in configuration (pinning above all) without one edit
// silently changing both.
void Kaspr::create_verify_tach_books()
{
    if (!verify_parallel_)
        return;
    if (!enable_tachbook_) {
        std::cerr << "Kaspr: cme_verify_parallel set but tachbook is OFF -- "
                     "no shadow books, no shadow samples" << std::endl;
        return;
    }
    if (verify_books_built_)
        return;
    verify_books_built_ = true;

    std::cerr << "Kaspr: Creating SHADOW TachBooks (verification path)" << std::endl;

    auto num_venues = en::x_num_syms();
    auto n = frame::ref::RefData::inst().num_assets();
    tach_books_v.resize(num_venues);
    for (size_t i = 0; i < tach_books_v.size(); i++)
        tach_books_v[i].resize(n, nullptr);

    // Pinned off the SAME list as the primary set, continuing the round robin.
    // That means shadow books can land on the same cpus as primary books. Said
    // plainly because it matters: if the list is short the two paths compete,
    // and the latency comparison is then partly a scheduling artefact. Give
    // tachbook_cpus at least 2x the books to avoid it.
    auto tb_cpu_vec = parse_cpu_list(tachbook_cpus_);
    size_t tb_i = es_tach_books.size() + nq_tach_books.size() + zn_tach_books.size();

    std::set<std::string> futures_mnemonics = {"ES", "NQ", "ZN"};
    for (size_t j = 1; j < n; j++)
    {
        auto a = frame::ref::RefData::inst().get_asset(j);
        if (a && futures_mnemonics.count(a->mnemonic) && a->get_exchange_md() == en::x::CMEMDFUT)
        {
            // "_P" suffix. Manager keys actors by name and asserts on a
            // duplicate (Manager.cpp:224), so the shadow set cannot reuse
            // TACHOB_<sym> -- the first shadow book aborts the process at
            // startup. The suffix matches the probe's _P file suffix, so a
            // shutdown census line names the same path as the csv it produced.
            auto tb = new frame::ob::act::TachBook(j, "_P");
            std::set<int> aff;
            if (!tb_cpu_vec.empty()) {
                aff.insert(tb_cpu_vec[tb_i % tb_cpu_vec.size()]);
                ++tb_i;
            }
            add_to_manage_q(tb, aff);
            tach_books_v[en::x::CMEMDFUT][j] = tb;

            if (a->mnemonic == "ES")      es_tach_books_v.push_back(tb);
            else if (a->mnemonic == "NQ") nq_tach_books_v.push_back(tb);
            else if (a->mnemonic == "ZN") zn_tach_books_v.push_back(tb);
        }
    }

    std::cerr << "Kaspr: Created " << (es_tach_books_v.size() + nq_tach_books_v.size()
                                       + zn_tach_books_v.size())
              << " SHADOW TachBooks" << std::endl;
}
#endif // KASPR_VERIFY_TEE

void Kaspr::create_probes()
{
    if (!enable_perf_probe_)
        return;

    if (!enable_tachbook_) {
        // Not a warning to bury in a log: the run produces no samples at all.
        std::cerr << "Kaspr: perf_probe requested but tachbook is OFF -- "
                     "nothing to subscribe to, no samples will be written"
                  << std::endl;
        return;
    }

    std::cerr << "Kaspr: Creating LatencyProbes (bin " << probe_bin_ms_ << " ms)" << std::endl;

    // One probe per TachBook. The tag carries "perf" because
    // TachBook::subscribe_handler admits a HI-priority subscriber only on that
    // substring -- rename it and the probe silently receives nothing.
    // suffix: "" in normal runs so existing analysis keeps working unchanged;
    // "_S" / "_P" only when the tee is on, so the two paths never write to the
    // same lat_<sym>.csv / .msg and silently interleave.
    auto attach = [&](cfsmp tb, const char *suffix) {
        auto sym = static_cast<frame::ob::act::TachBook *>(tb)->get_sym();
        auto a   = frame::ref::RefData::inst().get_asset(sym);
        if (!a) return;

        std::string tag = "perf_" + a->name + suffix;
        std::string csv;
        if (!probe_csv_dir_.empty())
            csv = probe_csv_dir_ + "/lat_" + a->name + suffix + ".csv";

        auto p = create_LatencyProbe(tb, sym, tag.c_str(), probe_bin_ms_, csv);
        add_to_manage_q(p);
        probes.push_back(p);

        std::cerr << "Kaspr:   probe " << tag
                  << " -> " << (csv.empty() ? std::string("(console only)") : csv)
                  << std::endl;
    };

    // In a default build there is no second path, so the suffix is always ""
    // and lat_<sym>.csv keeps the name every existing analysis script expects.
    const char *prim_sfx = "";
#ifdef KASPR_VERIFY_TEE
    if (verify_parallel_)
        prim_sfx = "_S";
#endif
    for (auto tb : es_tach_books) attach(tb, prim_sfx);
    for (auto tb : nq_tach_books) attach(tb, prim_sfx);
    for (auto tb : zn_tach_books) attach(tb, prim_sfx);

#ifdef KASPR_VERIFY_TEE
    for (auto tb : es_tach_books_v) attach(tb, "_P");
    for (auto tb : nq_tach_books_v) attach(tb, "_P");
    for (auto tb : zn_tach_books_v) attach(tb, "_P");
#endif

    std::cerr << "Kaspr: Created " << probes.size() << " LatencyProbes" << std::endl;
}
#endif

void Kaspr::create_support_modules()
{
    std::cerr << "Kaspr: Creating support modules" << std::endl;

    // Create console
    cons = create_CONS(this, "CONS");
    add_to_manage_q(cons);

    // Create MQ0_server for ZMQ monitoring
    auto mqport = pt_general.get<uint16_t>("kaspr.general.mqport", 9001);
    mq0 = create_MQ0_server("ZMQ", mqport, cons);
    add_to_manage_q(mq0);

    // Create timer - needs at least one OB to subscribe to for time
    if (!es_order_books.empty())
    {
        timer = new frame::mtim::act::Timer(es_order_books[0]);
        add_to_manage_q(timer);
        logger->set_timer(timer);
    }

    std::cerr << "Kaspr: Support modules created (port=" << mqport << ")" << std::endl;
}

void Kaspr::create_db()
{
    std::cerr << "Kaspr: Creating DB" << std::endl;
    db = create_DB(
        en::x::CMEMDFUT,
        order_books,
        som[en::x::CMEMDFUT]  // Pass SOM so DB can subscribe to fills
    );
    add_to_manage_q(db);
}

void Kaspr::create_som()
{
    std::cerr << "Kaspr: Creating SOM in sim_mode" << std::endl;

    // Read SOM config if available
    boost::property_tree::ptree pt_som;
    auto som_file = resolve_path(config_dir_, "som.ini");
    try {
        boost::property_tree::read_info(som_file, pt_som);
    } catch (...) {
        // Use empty config if file not found
    }

    // Create SOM for futures (CMEMDFUT) only
    // Pass DB as fill_subscriber so SOM stores local reference (no RemoteReplyProxy)
    auto s_fut = create_SOM(
        en::x::CMEMDFUT,
        db,
        pt_som,
        order_books,
        true,   // sim_mode - simulates fills against OB
        false,  // spin
        reset_positions_,  // reset positions on startup
        db      // fill_subscriber - DB subscribes to fills directly
    );
    add_to_manage_q(s_fut);
    som[en::x::CMEMDFUT] = s_fut;

    std::cerr << "Kaspr: SOM created for CMEMDFUT (DB subscribed to fills)" << std::endl;
}

void Kaspr::create_lights()
{
    std::cerr << "Kaspr: Creating lights for all instruments (4 per side)" << std::endl;

    // Read light config
    boost::property_tree::ptree pt_light;
    auto light_file = resolve_path(config_dir_, "light.ini");
    try {
        boost::property_tree::read_info(light_file, pt_light);
    } catch (...) {
        // Use defaults if file not found
    }

    const int NUM_LIGHTS_PER_SIDE = 4;

    // Helper to create lights for a single order book (8 lights: 4 BUY + 4 SEL)
    auto create_lights_for_ob = [&](cfsmp ob, en::x venue) {
        auto sym = static_cast<frame::ob::act::OB*>(ob)->get_sym();
        auto a = frame::ref::RefData::inst().get_asset(sym);
        if (!a) return;

        // Create coordinators for this symbol
        auto pcoord = create_PCoord();
        pcoord_map[a->name] = pcoord;

        // ONE QCoord PER BANK -- the buy lights share one, the sell lights
        // share another. That is what SHADOW_ALGORITHM.md has always described,
        // and it is what makes lev_orders_max a per-PRICE cap instead of a
        // per-light one: with a QCoord each, sz_at_px saw only that light's own
        // order, so four lights could rest 4 x lev_orders_max at a level and
        // none of them knew the others were there.
        //
        // The mmid is what blocked sharing. QCoord::mmid_orders is a uint64_t
        // bitmask with one bit per mmid and every call site passed 0, so all
        // four lights contended for bit 0 and one light's remove_order cleared
        // the flag for its siblings. Each light now gets its own mmid;
        // px_mmid_ord is already keyed px -> mmid -> ord, so they get separate
        // slots at a price while sz_at_px sums across the bank.
        auto qcoord_buy = create_QCoord();
        auto qcoord_sel = create_QCoord();

        // Create 4 buy lights
        for (int i = 0; i < NUM_LIGHTS_PER_SIDE; i++) {
            auto name = "L_" + a->name + "_BUY_" + std::to_string(i);
            auto light = create_light22_Shadow_BUY(
                "kaspr", db, nullptr, name,
                en::trader::SIMULATOR, a->name,
                venue, venue,
                qcoord_buy, pcoord, ob, nullptr, 0,
                timer, som[venue], i, pt_light, 0, false
            );
            add_to_manage_q(light);
            lights.push_back(light);
        }

        // Create 4 sell lights
        for (int i = 0; i < NUM_LIGHTS_PER_SIDE; i++) {
            auto name = "L_" + a->name + "_SEL_" + std::to_string(i);
            auto light = create_light22_Shadow_SEL(
                "kaspr", db, nullptr, name,
                en::trader::SIMULATOR, a->name,
                venue, venue,
                qcoord_sel, pcoord, ob, nullptr, 0,
                timer, som[venue], i, pt_light, 0, false
            );
            add_to_manage_q(light);
            lights.push_back(light);
        }

        std::cerr << "Kaspr: Created 8 lights for " << a->name << std::endl;
    };

    // Equity futures only (CMEMDFUT)
    for (auto ob : es_order_books)
        create_lights_for_ob(ob, en::x::CMEMDFUT);
    for (auto ob : nq_order_books)
        create_lights_for_ob(ob, en::x::CMEMDFUT);

    std::cerr << "Kaspr: Created " << lights.size() << " lights total" << std::endl;
}

void Kaspr::create_positionman()
{
    std::cerr << "Kaspr: Creating PositionManager" << std::endl;
    positionman = ::create_PositionManager("kaspr", pcoord_map);
    add_to_manage_q(positionman);
}

void Kaspr::create_mtd()
{
    std::cerr << "Kaspr: Creating MTD" << std::endl;
    mtd = create_MTD("MTD", som, order_books);
    add_to_manage_q(mtd);
}

void Kaspr::start_channel(const std::string& config_name, en::x venue)
{
    boost::property_tree::ptree pt_cme;
    auto cme_file = resolve_path(config_dir_, "cme.ini");
    boost::property_tree::read_info(cme_file, pt_cme);

    auto pt_chan = pt_cme.get_child("cme." + config_name);
    auto chan = pt_chan.get<int>("chan");
    auto cme_inifile = pt_chan.get<std::string>("cme_ini");

    std::cerr << "Kaspr: Starting channel " << chan << " (" << config_name << ")" << std::endl;

    boost::property_tree::ptree pt_mdp3;
    boost::property_tree::read_info(cme_inifile, pt_mdp3);

    auto chanstr = std::to_string(chan);
    auto p_cme = pt_mdp3.get_child("chan" + chanstr);

    // Create feed handler
    // UseFastSend=false, TreasOnly=false
    auto handler = new handler_if<false, false>(venue, chan);
    handler->mbo_order_books = order_books.at(venue);
    handler->binrec = nullptr;

#ifdef USE_TACHBOOK
    // Measurement mode. handler_if has ONE book vector, indexed by asset_id --
    // it feeds OB or TachBook, never both. So creating TachBooks is not enough
    // to measure them: without this swap they are constructed, started, and
    // then sit at zero messages forever, and the probe writes an empty CSV.
    //
    // This is destructive by construction, which is why it is a separate flag
    // and not implied by tachbook+perf_probe. With it on, the OBs get nothing,
    // so lights, SOM sim-fills, DB and MTD all see a dead market. That is fine
    // for a latency window and useless for anything else. Never set it on the
    // production recorder.
    if (enable_tachbook_ && enable_perf_probe_ &&
        pt_general.get<bool>("kaspr.general.perf_route_tachbook", false))
    {
        handler->mbo_order_books = tach_books.at(venue);
        std::cerr << "Kaspr: PERF MODE - channel " << chan
                  << " market data routed to TachBook; OB receives nothing"
                  << std::endl;
    }
#endif

    auto dorecovery = pt_chan.get<bool>("cme_do_recovery", true);
    auto disable_mbo = pt_chan.get<bool>("cme_disable_mbo", false);
    auto maxmpblevel = pt_chan.get<int>("cme_max_mbp_level", 0);

    // ---- decode subsystem: SERIAL by default, PARALLEL opt-in ----
    //
    // cme_decode_workers == 0 (the DEFAULT) selects the SERIAL path: DataDecoder
    // decodes every packet inline on the MessageProcessor thread via mbo_data,
    // straight into handler_if. No Reconstructor, no workers, no second orderid
    // map. This is the path the latency article measured.
    //
    // > 0 selects the PARALLEL path and MUST be a power of two (set_workers uses
    // nworkers-1 as a mask). Still NOT production-ready, but the reason has
    // changed and the old text here was wrong twice over.
    //
    // It used to abort on any packet mixing hot templates with
    // MDIncrementalRefreshVolume37, and this comment put that at "3.03% of live
    // ES packets (10.21% of messages)". Both figures came from a partial census
    // and are superseded: over the full 120,000-packet sample on live ES chan
    // 310 it is 3,351 packets (2.79%) and 12,436 messages (9.41%).
    //
    // It no longer aborts on them. DataDecoder::dispatch_split sends the hot
    // messages to the worker fleet and decodes the order-INDEPENDENT colds
    // inline; every cold message in all 3,351 mixed packets was Volume37, which
    // is order-independent. What still aborts is hot mixed with an order-
    // CRITICAL cold (ChannelReset4, SecurityStatus30, a definition, or an
    // unknown template): 0 of 120,000 observed, but unobserved is not
    // impossible, and the ordered barrier is not written yet.
    //
    // The remaining blockers are B3 (shadow instrument definitions), B5 (own
    // book set), B7 (EndOfBurst) and B8 (volume records). Until those land this
    // is a measurement path, not a recording path.
    //
    // The default used to be 8 and NO ini anywhere set the key, so every channel
    // silently ran the parallel path and died on its first mixed packet.
    uint32_t NWORKERS = pt_chan.get<uint32_t>("cme_decode_workers", 0);

    // Dual-path verify. When it is on the PRIMARY is forced SERIAL -- it is the
    // known-good reference and its DecodeResult is the only one that may drive
    // a recovery -- and cme_decode_workers sizes the SHADOW instead. Absent
    // key under verify means 2, which is the smallest count that exercises the
    // fleet at all.
    //
    // In a default build `verify` is a compile-time false, so PRIM_WORKERS is
    // NWORKERS, SHAD_WORKERS is 0, and every branch below that depends on it
    // folds away.
    bool verify = false;
#ifdef KASPR_VERIFY_TEE
    verify = verify_parallel_;
    if (verify && NWORKERS == 0)
        NWORKERS = 2;
#endif

    // set_workers masks with nworkers-1 (DataDecoder.hpp:84), so a count that
    // is not a power of two is silently rounded DOWN and nothing says so:
    // cme_decode_workers 12 ran 8 workers and reported 12. Refuse it.
    if (NWORKERS != 0 && (NWORKERS & (NWORKERS - 1)) != 0)
        throw std::runtime_error(
            "chan " + chanstr + ": cme_decode_workers must be a power of two "
            "(set_workers masks with nworkers-1, so " +
            std::to_string(NWORKERS) + " would silently run " +
            std::to_string(1u << (31 - __builtin_clz(NWORKERS))) + ")");

    const uint32_t PRIM_WORKERS = verify ? 0u : NWORKERS;
    const uint32_t SHAD_WORKERS = verify ? NWORKERS : 0u;
    (void)SHAD_WORKERS;  // unused when built without the tee

    mdp3::Reconstructor* recon   = nullptr;
    actor_ptr*           workers = nullptr;

    if (PRIM_WORKERS > 0)
    {
        std::cerr << "Kaspr: chan " << chanstr << " PARALLEL decode, "
                  << PRIM_WORKERS << " workers -- NOT production-ready" << std::endl;
        recon = new mdp3::Reconstructor(handler->mbo_order_books, venue, (uint32_t)chan, "P");
        workers = new actor_ptr[PRIM_WORKERS]; // process-lifetime; set_workers keeps this array
        for (uint32_t i = 0; i < PRIM_WORKERS; ++i)
            workers[i] = new mdp3::DecodeWorker(recon, venue, i, (uint32_t)chan, "P");
    }
    else
    {
        std::cerr << "Kaspr: chan " << chanstr << " SERIAL decode (inline)" << std::endl;
    }

    // Null on the serial path. handler_if guards every use of this pointer (the
    // AssetMap sends in the two definition handlers, and the ResetMBO send), so
    // null just means "nobody to tell".
    handler->reconstructor = recon;

    auto* decoder = new mdp3::DataDecoder(handler, disable_mbo, (uint32_t)maxmpblevel, /*debug=*/false,
                                          PRIM_WORKERS > 0 ? "P" : "S", (uint32_t)chan);
    decoder->set_workers(workers, PRIM_WORKERS); // (nullptr,0) on serial -> parallel_decode_ stays false

    // ---- dual-path verification: the SHADOW pipeline ----
    //
    // Same packets, fed from MessageProcessor's tee. Second DataDecoder,
    // second Reconstructor, second worker fleet, second book set. Nothing is
    // shared with the primary except the packet bytes, which is the whole
    // point: two paths that share a book cannot be diffed against each other.
#ifdef KASPR_VERIFY_TEE
    actor_ptr decoder_shadow = nullptr;

    if (verify)
    {
        if (tach_books_v.empty() || tach_books_v.at(venue).empty())
            throw std::runtime_error(
                "chan " + chanstr + ": cme_verify_parallel is set but the "
                "shadow TachBook set is empty. It needs tachbook and "
                "perf_probe both on, and create_verify_tach_books() must run "
                "before start_channel()");

        auto* handler_v = new handler_if<false, false>(venue, chan);
        handler_v->mbo_order_books = tach_books_v.at(venue);
        handler_v->binrec = nullptr;   // one recorder; two would double-write

        auto* recon_v = new mdp3::Reconstructor(handler_v->mbo_order_books,
                                                venue, (uint32_t)chan, "P");
        auto* workers_v = new actor_ptr[SHAD_WORKERS];
        for (uint32_t i = 0; i < SHAD_WORKERS; ++i)
            workers_v[i] = new mdp3::DecodeWorker(recon_v, venue, i,
                                                  (uint32_t)chan, "P");
        handler_v->reconstructor = recon_v;

        // Instrument definitions and ChannelReset only ever reach the PRIMARY
        // handler -- RecoveryProcessor is built with one feed_handler_if.
        // Without this line the shadow Reconstructor's asset_map_ stays empty,
        // route() drops every message, and the shadow books sit at zero while
        // the run looks healthy.
        handler->reconstructor_shadow = recon_v;

        auto* dv = new mdp3::DataDecoder(handler_v, disable_mbo,
                                         (uint32_t)maxmpblevel, /*debug=*/false,
                                         "P", (uint32_t)chan);
        dv->set_workers(workers_v, SHAD_WORKERS);

        // Told ONCE, here, that it is fed asynchronously -- exactly like its
        // worker count above, and for the same reason: this is a property of
        // this decoder INSTANCE, not of each packet.
        //
        // The primary is reached by fast_send, so its handler runs inline on
        // the MessageProcessor thread and reply() hands the DecodeResult
        // straight back. The shadow is reached by send(), so reply() would
        // instead queue a DecodeResult into MessageProcessor's mailbox.
        // MessageProcessor has no handler for it, so it would be dispatched
        // to nothing and deleted -- but only after occupying a mailbox slot,
        // inflating the very ingress qlen this whole exercise measures. The
        // shadow must stay silent.
        dv->set_async_input(true);

        // set_recovery_target is deliberately NOT called. DataDecoder asks its
        // recovery_target_ to recover when a parallel decode fails
        // (DataDecoder.hpp:944). If the shadow could do that, a bug on the
        // verification path would trigger a real CME recovery and corrupt the
        // reference run. Left null, a shadow decode failure is counted by
        // MessageProcessor and otherwise dropped.
        decoder_shadow = dv;

        add_to_manage_q(recon_v);
        for (uint32_t i = 0; i < SHAD_WORKERS; ++i)
            add_to_manage_q(workers_v[i]);
        add_to_manage_q(dv);

        std::cerr << "Kaspr: chan " << chanstr
                  << " VERIFY shadow = PARALLEL, " << SHAD_WORKERS
                  << " workers, " << tach_books_v.at(venue).size()
                  << " shadow books (primary forced SERIAL)" << std::endl;
    }
#endif // KASPR_VERIFY_TEE

    // Create MDP3 components
    auto mdp3cfsmp = create_all_mdp3(
        chanstr,
        handler,
        decoder,
        p_cme.get<uint16_t>("port_dr"),
        p_cme.get<uint16_t>("port_ir"),
        p_cme.get<std::string>("group_dr").c_str(),
        p_cme.get<std::string>("group_ir").c_str(),
        false,        // debug recover
        dorecovery,
        true,         // recovery on start
        disable_mbo,
        maxmpblevel,
        false,        // debug
        0,            // spinbuf (no spinning for kaspr)
        true,         // blocksock
        p_cme.get<uint16_t>("port_a"),
        p_cme.get<uint16_t>("port_b"),
        p_cme.get<std::string>("group_a").c_str(),
        p_cme.get<std::string>("group_b").c_str(),
        p_cme.get<std::string>("mdinterface_a").c_str(),
        p_cme.get<std::string>("mdinterface_b").c_str(),
        p_cme.get<std::string>("name").c_str()
#ifdef MDP3_VERIFY_TEE
        // null unless cme_verify_parallel; MessageProcessor tees to it
        , decoder_shadow
#endif
    );

    // Let the decoder ask the MessageProcessor to recover on a parallel decode failure.
    decoder->set_recovery_target(mdp3cfsmp[1]); // message_processor

    // Manage MDP3 actors.
    //
    // cme_cpus is a comma list of exactly 5 cpu ids, in the order the actors
    // are listed below:
    //
    //     recovery, message_processor, msg_buf_a, socket_proc_a, socket_proc_b
    //
    // Absent or empty => no pinning, which is the pre-existing behaviour and
    // what the production recorder gets. A list of any other length is a
    // hard error, not a partial application: pinning three of five actors and
    // saying nothing is exactly the silent-half-configured failure this
    // codebase keeps producing.
    //
    // socket_proc_a and socket_proc_b are the A and B sides of the same feed.
    // They are busy at the same instant, so do NOT put them on two SMT
    // siblings of one physical core -- they will fight for the same execution
    // units precisely during a burst. Siblings on this box are (N, N+32):
    // cpu16/cpu48 are one core, not two.
    auto cpu_str = pt_chan.get<std::string>("cme_cpus", "");
    auto cpus    = parse_cpu_list(cpu_str);

    if (!cpus.empty() && cpus.size() != 5) {
        throw std::runtime_error(
            "chan " + chanstr + ": cme_cpus needs exactly 5 cpu ids "
            "(recovery,msgproc,msgbuf,sock_a,sock_b), got " +
            std::to_string(cpus.size()) + " from \"" + cpu_str + "\"");
    }

    auto pin = [&](size_t i) -> std::set<int> {
        if (cpus.empty()) return {};
        std::cerr << "Kaspr:   chan " << chanstr << " actor[" << i
                  << "] -> cpu " << cpus[i] << std::endl;
        return {cpus[i]};
    };

    add_to_manage_q(mdp3cfsmp[0], pin(0));  // recovery_processor
    add_to_manage_q(mdp3cfsmp[1], pin(1));  // message_processor
    add_to_manage_q(mdp3cfsmp[2], pin(2));  // msg_buf_a
    add_to_manage_q(mdp3cfsmp[4], pin(3));  // socket_processor_a
    add_to_manage_q(mdp3cfsmp[5], pin(4));  // socket_processor_b

    // Manage the decode subsystem. On the serial path recon/workers do not
    // exist, so there is exactly one actor here: the DataDecoder.
    if (recon)
    {
        add_to_manage_q(recon);
        for (uint32_t i = 0; i < PRIM_WORKERS; ++i)
            add_to_manage_q(workers[i]);
    }
    add_to_manage_q(decoder);

    std::cerr << "Kaspr: MDP3 channel " << chan << " configured" << std::endl;
}

void Kaspr::start_market_data()
{
    std::cerr << "Kaspr: Starting MDP3 market data" << std::endl;

    // Channel 310 - ES futures (CMEMDFUT)
    if (pt_general.get<bool>("kaspr.channels.chan_310", false)) {
        start_channel("prod_equity", en::x::CMEMDFUT);
    }

    // Channel 318 - NQ futures (CMEMDFUT)
    if (pt_general.get<bool>("kaspr.channels.chan_318", false)) {
        start_channel("prod_nasdaq", en::x::CMEMDFUT);
    }

    // Channel 344 - CBOT interest rate futures, ZN/ZF/ZB/ZT/UB (CMEMDFUT)
    //
    // This branch did not exist. kaspr.ini has carried chan_344 true for as
    // long as the setting has been there, and it did nothing: the flag was
    // read by nobody, so no treasury future has ever reached a book on this
    // path. The universe rows, the zn_order_books vector in the header and
    // the prod_treasury_futures section in cme.ini were all already present,
    // which is why it looked wired.
    //
    // A config key nothing reads is silent by construction. That is the same
    // failure as the TachBook ladder: the system reported normal operation
    // while producing nothing.
    if (pt_general.get<bool>("kaspr.channels.chan_344", false)) {
        start_channel("prod_treasury_futures", en::x::CMEMDFUT);
    }
}

} // namespace kaspr

// Graceful shutdown flag
static volatile sig_atomic_t running = 1;

static void shutdown_handler(int signum)
{
    (void)signum;
    running = 0;
}

int main(int argc, char* argv[])
{
    // Save args for debugging
    SET_ARGS;
    PRINT_ARGS;

    // Set timezone
    tzset();

    // Install crash signal handlers
    ::signal(SIGSEGV, &my_signal_handler);
    ::signal(SIGABRT, &my_signal_handler);
    ::signal(SIGBUS,  &my_signal_handler);
    ::signal(SIGFPE,  &my_signal_handler);
    ::signal(SIGPIPE, &my_signal_handler);

    // Install graceful shutdown handlers
    ::signal(SIGINT,  &shutdown_handler);
    ::signal(SIGTERM, &shutdown_handler);

    std::string config_file = "config/kaspr.ini";
    bool reset_positions = false;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--reset-positions") {
            reset_positions = true;
            std::cout << "kaspr: Position reset requested" << std::endl;
        } else if (i == 1 && arg[0] != '-') {
            // First non-flag argument is config file
            config_file = arg;
        }
    }

    std::cout << "=== Kaspr Trading System (ES Simulation) ===" << std::endl;
    std::cout << "pid: " << getpid() << std::endl;

    try {
        kaspr::Kaspr mgr(config_file, reset_positions);

        // Enable floating point exception trapping.
        // feenableexcept is a glibc extension (Linux); not available on macOS.
#ifndef __APPLE__
        feenableexcept(FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INVALID);
#endif

        std::cout << "Kaspr: Initializing actors..." << std::endl;
        mgr.init();

        std::cout << "Kaspr: Running... (Press Ctrl+C to stop)" << std::endl;

        // Main loop - wait for shutdown signal
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "Kaspr: Shutting down..." << std::endl;
        mgr.end();

    } catch (const std::exception& e) {
        std::cerr << "Kaspr: Fatal error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "=== Kaspr Shutdown Complete ===" << std::endl;
    return 0;
}
