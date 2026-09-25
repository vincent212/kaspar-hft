#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include <cstdint>

#include <boost/format.hpp>
#include "chutil/Macros.hpp"
#include "chutil/Assert.hpp"
#include "actors/Actor.hpp"
#include "mdp3/DataDecoder.hpp"     // DataDecoder::decode_one
#include "mdp3/DecodeSink.hpp"
#include "mdp3/msg/DecodeReq.hpp"
#include "mdp3/msg/DecodeDone.hpp"

namespace mdp3
{
  // One of N warm, pinned decode workers. Receives a DecodeReq pointing at a
  // single SBE message inside a live packet buffer (zero-copy) and runs the very
  // same per-message decode the inline path runs -- DataDecoder::decode_one --
  // but drives a stateless DecodeSink that forwards each built entry to the
  // Reconstructor instead of touching maps or books. (One decode_one, shared by
  // the inline loop and the workers -- no duplicated switch.)
  //
  // Only hot MBO templates (46/47 book, 48 trade) are ever dispatched here; the
  // coordinator keeps everything else on the inline serial path. We assert that
  // contract on entry.
  //
  // After decoding, it replies DecodeDone{parent_id} to the coordinator (the
  // DecodeReq's sender) so the coordinator can drop the packet buffer's refcount
  // once every worker that read it is done.
  class DecodeWorker : public actors::Actor
  {
  public:
    // chan is part of the name: "DecodeWorker_%u" was unique only within one
    // channel, so worker 0 of a second channel collided in Manager. "_P" marks
    // the parallel path (there is no serial equivalent of this actor), matching
    // DataDecoder_P_310 / Reconstructor_P_310.
    DecodeWorker(actor_ptr reconstructor, en::x xchg, uint32_t worker_id,
                 uint32_t chan, const char *path_tag = "P")
        : sink_(reconstructor, this, xchg)
    {
      snprintf(name_, sizeof(name_), "DecodeWorker_%s_%u_%u", path_tag, chan, worker_id);
      MESSAGE_HANDLER(msg::DecodeReq, on_decode);
    }

    const char *get_name() const override { return name_; }

  private:
    void on_decode(const msg::DecodeReq *req) noexcept
    {
      // Every message is dispatched here now (no hot/cold filter). decode_one
      // dispatches on template; the sink builds entries for the ones it handles
      // (book/trade/definition/reset) and no-ops the rest -- an empty batch still
      // flushes so the Reconstructor's order_seq advances.

      // Seed the sink for this message: its entries are accumulated into a batch
      // and flushed below as one ParsedMsg tagged with this order_seq.
      sink_.order_seq_ = req->order_seq;
      sink_.ingress_qlen_ = req->qlen;

      bool is_channel_reset = false; // reset is applied via the l3_chr_v2_t entry
      const bool ok = DataDecoder::decode_one(const_cast<char *>(req->msg), req->len, req->ts,
                                              req->msg_seq, req->sending_time, &sink_,
                                              is_channel_reset, /*debug=*/false);

      // Send this message's entries (one batch, tagged order_seq) to the Reconstructor.
      sink_.flush();

      // Signal the coordinator we are done reading this packet buffer. Sent AFTER
      // all ParsedEntry sends above, so the refcount only drops once the buffer
      // read is complete. reply() routes to the DecodeReq's sender (coordinator).
      reply(new msg::DecodeDone(req->parent_id, ok));
    }

    DecodeSink sink_;
    char       name_[256];
  };
}
