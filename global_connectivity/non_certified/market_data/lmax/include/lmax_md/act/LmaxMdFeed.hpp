/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LmaxMdFeed: LMAX Broker Market Data (FIX 4.4) into Kaspar records, one
// OB per instrument. NOT CERTIFIED: see README.md for what has been checked.
//
//   QuickFIX initiator (TLS)  logon with Username/Password and 141=Y
//     -> MarketDataRequest per instrument (snapshot + updates, depth, bid,
//        offer, trade)
//     -> each W parsed on QuickFIX's thread into an MdEvent, sent here
//     -> LmaxMdSync (snapshots diffed, trades) -> Mbp2Mbo
//     -> frame::mda::msg::Data to the instrument's OB
//
// Sequence gaps (feed.md section 5, review.md decision 3). LMAX's market-data
// gateway "does not support any FIX message recovery". QuickFIX answers a
// MsgSeqNum gap with a ResendRequest and holds later messages until the gap
// is filled. The feed sees the ResendRequest go out (toAdmin) and sends a gap
// record to every book. The gap counts as closed once books arrive and no
// further ResendRequest has gone out for gap_timeout_ms; if it is not closed
// within twice that from the first ResendRequest, the feed drops the
// connection; QuickFIX logs on again with 141=Y and the next W of each
// instrument is sent to OB as a recovery snapshot (after a book-clear record).
// A logout or disconnect does the same.
//
// Threads: QuickFIX's thread only parses and send()s MdEvents. Everything
// else happens on this actor's thread. A ticker thread sends a TICK every
// 200 ms for the gap and stall timers.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "actors/Actor.hpp"
#include "actors/Message.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "sim/Mbp2Mbo.hpp"

#include "lmax_fix/FixInitiator.hpp"
#include "lmax_md/LmaxMdSync.hpp"
#include "lmax_md/MdParse.hpp"

namespace lmax::md
{
  struct MdFeedConfig
  {
    fix::SessionParams session; // env, host, port, tls, CompIDs, dictionary
    std::string username, password; // from the key file; never in the repository
    std::vector<Instrument> instruments;
    int depth = 20;          // MarketDepth asked for; LMAX's maximum is 20
    bool trades = true;      // ask for MDEntryType 2
    char venue = 0;          // en::x value in the records
    int gap_timeout_ms = 2000; // after a ResendRequest, wait this long for a W, then reconnect
    int stall_ms = 0;        // no W at all for this long while logged on: reconnect (0: off)

    // key = value file (config/lmax_md_example.ini) plus the key file it
    // names; false with the reason in err.
    static bool from_file(const std::string &path, MdFeedConfig &out, std::string &err);
  };

  namespace msg
  {
    struct MdEvent : public actors::MessageT<MdEvent>
    {
      enum Kind
      {
        LOGON,
        LOGOUT,
        SNAPSHOT,
        MD_REJECT,
        SESSION_REJECT,
        RESEND_SENT, // QuickFIX asked LMAX to resend: a gap
        SEQ,         // MsgSeqNum of a message QuickFIX delivered
        BAD_MESSAGE, // a W or Y that did not parse
        TICK,
      };
      Kind kind;
      MdSnapshot snap;
      MdReject reject;
      std::string text;
      uint64_t seq = 0, seq2 = 0;
      uint64_t next = 0;     // SEQ: the MsgSeqNum expected after this message
      bool possdup = false;  // SEQ: PossDupFlag=Y
      uint64_t recv_ns = 0;
      explicit MdEvent(Kind k) : kind(k) {}
    };
  }

  // Observer for the live check and the tests: called on the feed's thread.
  class MdTap
  {
  public:
    virtual ~MdTap() = default;
    virtual void on_record(size_t inst, const bfile::l3_t &r) {}
    virtual void on_snapshot(size_t inst, const MdSnapshot &s, const LmaxMdSync &sync) {}
    virtual void on_session(const std::string &event) {}
  };

  class LmaxMdFeed : public actors::Actor
  {
  public:
    struct Counters
    {
      uint64_t logons = 0, logouts = 0;
      uint64_t resend_requests = 0; // gaps QuickFIX saw
      uint64_t forced_reconnects = 0;
      uint64_t md_rejects = 0, session_rejects = 0, bad_messages = 0;
      uint64_t seq_breaks = 0;      // delivered MsgSeqNum not last + 1 (outside a logon)
      uint64_t messages = 0;        // delivered messages, admin and application
      uint64_t records = 0;
    };

    // obs[i]: the OB of instruments[i] (nullptr: records go only to the tap).
    LmaxMdFeed(MdFeedConfig cfg, std::vector<actors::Actor *> obs, MdTap *tap = nullptr);
    ~LmaxMdFeed() override;
    const char *get_name() const override { return "LmaxMdFeed"; }

    // Read on the actor's thread, or after it has stopped.
    const Counters &counters() const { return c_; }
    const LmaxMdSync &sync(size_t i) const { return *syncs_[i]; }
    size_t instruments() const { return syncs_.size(); }
    bool logged_on() const { return logged_on_.load(); }

  private:
    class App;

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void event_handler(const msg::MdEvent *) noexcept;
    void subscribe() noexcept;
    void gap_to_books() noexcept;
    void reset_books() noexcept;
    void emit(size_t i, const bfile::l3_t &r) noexcept;

    MdFeedConfig cfg_;
    std::vector<actors::Actor *> obs_;
    MdTap *tap_;
    std::vector<std::unique_ptr<sim::Mbp2Mbo>> chunks_;
    std::vector<std::unique_ptr<LmaxMdSync>> syncs_;
    std::unique_ptr<App> app_;
    std::unique_ptr<fix::FixInitiator> fix_;
    std::jthread ticker_;
    std::atomic<bool> logged_on_{false};
    Counters c_;
    uint64_t next_req_ = 1;
    uint64_t next_seq_ = 0; // MsgSeqNum expected next; 0 before the first message
    uint64_t gap_deadline_ns_ = 0; // 0: no gap outstanding
    uint64_t last_resend_ns_ = 0;
    uint64_t last_w_ns_ = 0;
    uint64_t logon_ns_ = 0;
  };
}

// nullptr, with the reason on stderr, for a bad config.
actors::Actor *create_LmaxMdFeed(const std::string &config_path, std::vector<actors::Actor *> obs);
