/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
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
#endif

    // Create Logger first so log_inf works
    logger = new polonaise::logger::act::Logger("kaspr_log", nullptr);
    add_to_manage_q(logger);

    // Create components in order
    create_order_books();
#ifdef USE_TACHBOOK
    create_tach_books();
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

    std::set<std::string> futures_mnemonics = {"ES", "NQ"};
    for (size_t j = 1; j < n; j++)
    {
        auto a = frame::ref::RefData::inst().get_asset(j);
        if (a && futures_mnemonics.count(a->mnemonic) && a->get_exchange_md() == en::x::CMEMDFUT)
        {
            std::cerr << "Kaspr: Creating TachBook for " << a->name
                      << " (" << a->mnemonic << ", id=" << j << ")" << std::endl;

            auto tb = new frame::ob::act::TachBook(j);
            add_to_manage_q(tb);
            tach_books[en::x::CMEMDFUT][j] = tb;

            if (a->mnemonic == "ES") {
                es_tach_books.push_back(tb);
            } else if (a->mnemonic == "NQ") {
                nq_tach_books.push_back(tb);
            }
        }
    }

    std::cerr << "Kaspr: Created " << es_tach_books.size() << " ES TachBooks" << std::endl;
    std::cerr << "Kaspr: Created " << nq_tach_books.size() << " NQ TachBooks" << std::endl;
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

        // Create 4 buy lights
        for (int i = 0; i < NUM_LIGHTS_PER_SIDE; i++) {
            auto qcoord = create_QCoord();
            auto name = "L_" + a->name + "_BUY_" + std::to_string(i);
            auto light = create_light22_Shadow_BUY(
                "kaspr", db, nullptr, name,
                en::trader::SIMULATOR, a->name,
                venue, venue,
                qcoord, pcoord, ob, nullptr, 0,
                timer, som[venue], 0, pt_light, 0, false
            );
            add_to_manage_q(light);
            lights.push_back(light);
        }

        // Create 4 sell lights
        for (int i = 0; i < NUM_LIGHTS_PER_SIDE; i++) {
            auto qcoord = create_QCoord();
            auto name = "L_" + a->name + "_SEL_" + std::to_string(i);
            auto light = create_light22_Shadow_SEL(
                "kaspr", db, nullptr, name,
                en::trader::SIMULATOR, a->name,
                venue, venue,
                qcoord, pcoord, ob, nullptr, 0,
                timer, som[venue], 0, pt_light, 0, false
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

    auto dorecovery = pt_chan.get<bool>("cme_do_recovery", true);
    auto disable_mbo = pt_chan.get<bool>("cme_disable_mbo", false);
    auto maxmpblevel = pt_chan.get<int>("cme_max_mbp_level", 0);

    // Create MDP3 components
    auto mdp3cfsmp = create_all_mdp3(
        chanstr,
        handler,
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
    );

    // Manage MDP3 actors (no CPU pinning for kaspr)
    add_to_manage_q(mdp3cfsmp[0]);  // recovery_processor
    add_to_manage_q(mdp3cfsmp[1]);  // message_processor
    add_to_manage_q(mdp3cfsmp[2]);  // msg_buf_a
    add_to_manage_q(mdp3cfsmp[4]);  // socket_processor_a
    add_to_manage_q(mdp3cfsmp[5]);  // socket_processor_b

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
