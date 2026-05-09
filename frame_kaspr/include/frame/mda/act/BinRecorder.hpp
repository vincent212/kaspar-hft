#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include "zlib.h"

#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"

// msg
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "frame/mda/msg/Data.hpp"

namespace frame::mda::act
{
  class BinRecorder : public actors::Actor
  {
    std::string outfilename;
    gzFile outf;
    const char* get_name() const { return "BinRec"; }

  public:
    BinRecorder(
        const std::string &_outfilename)
        : outfilename(_outfilename)
    {
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(mda::msg::Data, data_handler);
    }

  private:
    void data_handler(const msg::Data *m)
    {
      bfile::write_l3(outf, m->l3);
    }
    void start_handler(const actors::msg::Start *)
    {
      outf = gzopen(outfilename.c_str(), "wb");
      ASSERT(outf, "not open");
    }
    void shutdown_handler(const actors::msg::Shutdown *)
    {
      // gzflush(Z_FINISH) writes the gzip END marker and flushes pending
      // data to disk without freeing the deflate state. gzclose() additionally
      // frees the internal heap, but downstream allocations in the arena are
      // corrupted (confirmed in core dumps), so the final free aborts with
      // "double free or corruption (out)". Leaking the deflate state on
      // process exit is harmless.
      gzflush(outf, Z_FINISH);
    }
  };
}

actors::Actor* create_BinRecorder(const std::string &_outfilename);
