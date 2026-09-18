#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * BboRecorder - passive top-of-book tape writer.
 *
 * Subscribes (via BBBOSub) to every OB it is handed at construction, then
 * writes each incoming BBBOChg as one gzipped CSV row:
 *
 *     tx_time,venue,sym,best_bid,best_ask
 *
 * tx_time is nanoseconds since the epoch; venue is the numeric en::x code;
 * sym is the internal asset id (RefData id, NOT the CME securityID); best_bid
 * and best_ask are tick indices in the OB's ladder (i.e. price / minPriceIncrement).
 * Recording ticks rather than a decoded price keeps this actor decoupled from
 * per-symbol tick sizes -- downstream analysis multiplies by minPriceIncrement
 * from the universe.
 *
 * The BBBOChg firehose is already throttled inside OB.cpp
 * (act::OB::notifybbbosubs, 100 ms minimum between notifications per book), so
 * this actor does not throttle again -- one row per delivered BBBOChg.
 *
 * Shutdown pattern mirrors BinRecorder: gzflush(Z_FINISH) writes the gzip
 * trailer but does NOT gzclose, because gzclose frees deflate state that other
 * arena allocations already share.
 */

#include <string>
#include <vector>
#include "zlib.h"

#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "frame/ob/msg/BBBOChg.hpp"
#include "frame/ob/msg/BBBOSub.hpp"

namespace frame::ob::act
{
  class BboRecorder : public actors::Actor
  {
    std::string outfilename;
    std::string session_date;
    std::vector<actors::Actor*> obs;
    gzFile outf = nullptr;
    uint64_t row_count = 0;
    const char* get_name() const override { return "BboRec"; }

  public:
    BboRecorder(
        const std::string &_outfilename,
        const std::string &_session_date,
        const std::vector<actors::Actor*> &_obs)
        : outfilename(_outfilename),
          session_date(_session_date),
          obs(_obs)
    {
      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(frame::ob::msg::BBBOChg, bbbochg_handler);
    }

    uint64_t get_row_count() const { return row_count; }

  private:
    void start_handler(const actors::msg::Start *)
    {
      outf = gzopen(outfilename.c_str(), "wb");
      ASSERTF(outf, boost::format("BboRecorder: gzopen failed for %s") % outfilename);
      // Column-only header; the session date is encoded in the filename, so we
      // don't stamp it on every row.
      gzprintf(outf, "tx_time,venue,sym,best_bid,best_ask\n");

      int n_sent = 0;
      for (auto ob : obs) {
        if (ob) {
          ob->send(new frame::ob::msg::BBBOSub(), this);
          ++n_sent;
        }
      }
      std::cerr << "BboRecorder: session=" << session_date
                << " out=" << outfilename
                << " subscribed to " << n_sent << " OBs" << std::endl;
    }

    void bbbochg_handler(const frame::ob::msg::BBBOChg *m)
    {
      // %llu portable across 32/64-bit; tx_time is uint64_t ns since epoch.
      gzprintf(outf, "%llu,%u,%u,%d,%d\n",
               static_cast<unsigned long long>(m->tx_time),
               unsigned(m->venue.value),
               m->sym,
               m->best_bid,
               m->best_ask);
      ++row_count;
    }

    void shutdown_handler(const actors::msg::Shutdown *)
    {
      // gzflush(Z_FINISH) writes the gzip END marker and flushes pending data
      // to disk without freeing the deflate state. gzclose would additionally
      // free the internal heap, but downstream allocations in the arena are
      // corrupted by that (see BinRecorder for the same note). Leaking the
      // deflate state on process exit is harmless.
      if (outf) gzflush(outf, Z_FINISH);
      std::cerr << "BboRecorder: shutdown, rows written = " << row_count << std::endl;
    }
  };
}
