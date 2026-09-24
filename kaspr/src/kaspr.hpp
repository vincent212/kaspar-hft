/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
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
#include "mdp3/DataDecoder.hpp"
#include "mdp3/act/DecodeWorker.hpp"
#include "mdp3/act/Reconstructor.hpp"

//
// Frame (kaspr uses frame_kaspr for runtime components)
//
#include "frame/ref/RefData.hpp"

//
// Interfaces (from interface directory - these are factory functions)
//
#include "frame/ob/if/OB.hpp"
#ifdef USE_TACHBOOK
// light/if/TachBook.hpp (create_TachBook -> light::tachbook::TachBook) used to
// be included here. It was never called: kaspr builds its books from
// frame::ob::act::TachBook, included above. The light copy was a second,
// separately-maintained implementation of the same class and has been removed.
#include "interface/frame/perf/if/LatencyProbe.hpp"
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
    std::vector<cfsmp> zn_tach_books;   // treasury futures, channel 344
    bool enable_tachbook_ = false;

    // SHADOW book set for the dual-path decode verification tee. A SECOND,
    // fully independent TachBook per measured instrument, fed only by the
    // shadow decoder. The two paths must never share a book or the comparison
    // is meaningless -- the point is to diff their output.
    // Empty unless some channel sets cme_verify_parallel.
    std::vector<std::vector<cfsmp>> tach_books_v;
    std::vector<cfsmp> es_tach_books_v;
    std::vector<cfsmp> nq_tach_books_v;
    std::vector<cfsmp> zn_tach_books_v;
    bool verify_books_built_ = false;

    // Dual-path decode verification. General-section, not per-channel: the
    // shadow TachBooks and their probes are built in create_tach_books() /
    // create_probes(), which run BEFORE any channel config is read. A
    // per-channel key could not be honoured at that point.
    bool verify_parallel_ = false;

    // LatencyProbe - one per measured TachBook. Off unless the config asks
    // for it, so the production recorder never pays for a subscriber it did
    // not ask for. Attaching a probe is NOT free of observer effect: it adds
    // a real subscriber to TachBook's fan-out, so the book does work it would
    // not otherwise do. That cost lands in leg 2, not leg 1.
    std::vector<cfsmp> probes;
    bool enable_perf_probe_ = false;
    int  probe_bin_ms_ = 100;
    std::string probe_csv_dir_;
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
    // Position reset flag (for forward testing)
    //
    bool reset_positions_ = false;

    //
    // CPU pinning
    //
    // Empty by default, everywhere. An empty affinity set makes
    // Manager::set_thread_affinity return 0 without touching the thread, so a
    // config that does not mention cpus produces the exact binary behaviour
    // this system had before pinning existed. The production recorder is
    // therefore unaffected unless someone edits its ini.
    //
    // Only sched_setaffinity is used. SCHED_FIFO is NOT set: this box gives
    // the account CapEff=0 and `ulimit -r` 0, so pthread_setschedparam would
    // fail with EPERM and Manager would print "could not set priority" and
    // carry on unpinned-in-priority. Pinning without priority still leaves a
    // pinned thread preemptible by anything else scheduled on that cpu, which
    // is why the cpu list matters more than it would on an isolcpus box.
    // Nothing is isolated here -- /proc/cmdline has empty isolcpus, nohz_full
    // and rcu_nocbs.
    //
    std::string tachbook_cpus_;

    /**
     * Parse "16,48,17" into the vector [16,48,17]. Empty or whitespace-only
     * yields an empty vector, which means "do not pin".
     *
     * Returns a vector, NOT a set, on purpose. Callers assign by position --
     * cme_cpus is read as (recovery, msgproc, msgbuf, sock_a, sock_b) -- so a
     * set would silently re-sort the list and pin every actor to the wrong
     * core while the ini looked right. It also would not preserve duplicates,
     * and deliberately putting two actors on one cpu is a legitimate thing to
     * want to test.
     *
     * Out-of-range ids are NOT dropped here: Manager::add_to_manage_q asserts
     * on them, and an assert at startup is the correct outcome for a typo in
     * a cpu list. Discovering the pin never happened by reading a latency
     * histogram three hours later is not.
     */
    static std::vector<int> parse_cpu_list(const std::string& s);

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

    /**
     * Build the SHADOW TachBook set for the verification tee. Idempotent --
     * several channels may ask for it; only the first call builds.
     */
    void create_verify_tach_books();

    /**
     * Attach a LatencyProbe to each TachBook, if the config asks for it.
     * Must run AFTER create_tach_books() -- there is nothing to subscribe to
     * before that.
     */
    void create_probes();
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
};

} // namespace kaspr
