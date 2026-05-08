/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * kaspr.hpp - Kaspr Trading System Manager
 *
 * A simplified trading system for ES futures in simulation mode.
 * Uses OB.cpp (not TachBook) for order books.
 *
 * Components:
 * - MDP3 market data (channel 310 for ES)
 * - OB.cpp order books
 * - Lights for order execution
 * - Hedger for Python strategy interface
 * - SOM in sim_mode (simulates fills from OB)
 * - CONS (console handler)
 * - MQ0_server (ZMQ interface for MTD)
 * - MTD (monitoring)
 *
 * Copyright 2025 Vincent Maciejewski, & M2 Tech
 */

#pragma once

#include <map>
#include <string>
#include <vector>
#include <set>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/info_parser.hpp>

//
// Actor framework
//
#include "actors/act/Manager.hpp"
#include "actors/ActorRef.hpp"
#include "actors/remote/ZmqSender.hpp"
#include "actors/remote/ZmqReceiver.hpp"

//
// Frame components (OB, Timer)
//
#include "frame/ob/act/OB.hpp"
#ifdef USE_TACHBOOK
#include "frame/ob/act/TachBook.hpp"
#endif
#include "frame/mtim/act/Timer.hpp"

//
// MDP3 market data
//
#include "mdp3/if/mdp3.hpp"
#include "mdp3/handler_if.hpp"

//
// Frame (kaspr uses frame_kaspr for runtime components)
//
#include "frame/ref/RefData.hpp"

//
// Interfaces (from interface directory - these are factory functions)
//
#include "frame/ob/if/OB.hpp"
#ifdef USE_TACHBOOK
#include "light/if/TachBook.hpp"
#endif
#include "frame/mtim/if/Timer.hpp"
#include "cons/if/Cons.hpp"
#include "mtd/if/MTD.hpp"
#include "som/if/SOM.hpp"

//
// Light trading
//
#include "light/if/light22.hpp"
#include "light/if/QCoord.hpp"
#include "light/if/PCoord.hpp"
#include "positionman/if/PositionManager.hpp"

//
// ZMQ / MQ0
//
#include "mq0/if/MQ0_server.hpp"

//
// DB
//
#include "db/if/DB.hpp"

//
// Logger
//
#include "logger/act/Logger.hpp"

namespace kaspr {

/**
 * Kaspr Manager - Main trading system manager for ES futures sim
 *
 * Manages:
 * - MDP3 market data (channel 310 for ES futures)
 * - OB.cpp order books (not TachBook)
 * - Lights for order execution
 * - Hedger for Python strategy interface (via ZMQ)
 * - SOM in sim_mode (simulates fills against OB)
 * - Console (CONS) and MQ0_server for monitoring
 * - MTD for monitoring
 */
struct Kaspr : public actors::Manager
{
    // Configuration
    boost::property_tree::ptree pt_general;
    std::string config_file_;
    std::string config_dir_;  // Directory containing config files

    //
    // Order books - indexed by [venue][asset_id]
    // Venues: CMEMDFUT (futures), CMEMD (cash)
    //
    std::vector<std::vector<cfsmp>> order_books;

    // Equity futures (channel 310)
    std::vector<cfsmp> es_order_books;  // ES - E-mini S&P 500
    std::vector<cfsmp> nq_order_books;  // NQ - E-mini Nasdaq 100

#ifdef USE_TACHBOOK
    // TachBook (MBO L3) - parallel to OB, silent (no subscribers)
    std::vector<std::vector<cfsmp>> tach_books;
    std::vector<cfsmp> es_tach_books;
    std::vector<cfsmp> nq_tach_books;
    bool enable_tachbook_ = false;
#endif

    // Treasury futures (channel 344, CMEMDFUT venue)
    std::vector<cfsmp> zn_order_books;  // ZN (10-year futures)
    std::vector<cfsmp> zf_order_books;  // ZF (5-year futures)
    std::vector<cfsmp> zb_order_books;  // ZB (30-year futures)
    std::vector<cfsmp> zt_order_books;  // ZT (2-year futures)
    std::vector<cfsmp> ub_order_books;  // UB (ultra futures)

    //
    // Support actors
    //
    polonaise::logger::act::Logger* logger = nullptr;
    cfsmp timer = nullptr;
    cfsmp cons = nullptr;
    cfsmp mq0 = nullptr;
    cfsmp db = nullptr;
    cfsmp mtd = nullptr;

    //
    // SOM - Simulated Order Manager
    // In sim_mode=true, SOM simulates fills against OB instead of sending to iLink
    //
    std::map<en::x, cfsmp> som;

    //
    // Lights - order execution actors (buy/sell per symbol)
    //
    std::vector<cfsmp> lights;
    std::map<std::string, light::PCoord*> pcoord_map;  // instrument name -> PCoord

    //
    // PositionManager - tracks positions per instrument
    //
    cfsmp positionman = nullptr;

    //
    // Remote messaging (GlobalRegistry + ZMQ)
    //
    std::shared_ptr<actors::ZmqSender> zmq_sender_ = nullptr;
    actors::ZmqReceiver* zmq_receiver_ = nullptr;
    std::string registry_address_;
    int zmq_port_ = 0;

    //
    // Position reset flag (for forward testing)
    //
    bool reset_positions_ = false;

    //
    // Constructor
    //
    Kaspr(const std::string& config_file, bool reset_positions = false);
    ~Kaspr() = default;

    // Disable copy
    Kaspr(const Kaspr&) = delete;
    Kaspr& operator=(const Kaspr&) = delete;

private:
    //
    // Setup methods - called in order during construction
    //

    /**
     * Create order books for ES futures
     * Uses OB.cpp (not TachBook) from frame_kaspr
     */
    void create_order_books();

#ifdef USE_TACHBOOK
    /** Create TachBook (MBO L3) books in parallel with OB. */
    void create_tach_books();
#endif

    /**
     * Create support modules: CONS, MQ0_server, Timer
     */
    void create_support_modules();

    /**
     * Create DB actor for data queries
     */
    void create_db();

    /**
     * Create SOM in sim_mode
     * SOM simulates fills against OB instead of sending to iLink
     */
    void create_som();

    /**
     * Create lights for order execution
     */
    void create_lights();

    /**
     * Create PositionManager to track positions per instrument
     */
    void create_positionman();

    /**
     * Create MTD for monitoring
     */
    void create_mtd();

    /**
     * Start MDP3 market data for enabled channels
     */
    void start_market_data();


    /**
     * Start a single MDP3 channel
     * @param config_name Config section name in cme.ini (e.g., "prod_equity")
     * @param venue Exchange venue (CMEMDFUT or CMEMD)
     */
    void start_channel(const std::string& config_name, en::x venue);

    /**
     * Set up ZMQ remote messaging and GlobalRegistry connection
     * Enables Python actors to discover and communicate with C++ actors
     */
    void setup_remote_messaging();
};

} // namespace kaspr
