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
#include "mdp3/DataDecoder.hpp"     // DataDecoder::decode_one + is_hot_template
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
    DecodeWorker(actor_ptr reconstructor, en::x xchg, uint32_t worker_id)
        : sink_(reconstructor, this, xchg)
    {
      snprintf(name_, sizeof(name_), "DecodeWorker_%u", worker_id);
      MESSAGE_HANDLER(msg::DecodeReq, on_decode);
    }

    const char *get_name() const override { return name_; }

  private:
    void on_decode(const msg::DecodeReq *req) noexcept
    {
      // Contract: only hot templates reach a worker (coordinator's scan filters).
      // A non-hot template here is a dispatch bug -> abort in debug.
      const uint16_t TemplateID = *reinterpret_cast<const uint16_t *>(req->msg + 4);
      ASSERTF(DataDecoder::is_hot_template(TemplateID),
              boost::format("non-hot template %d dispatched to DecodeWorker") % TemplateID);

      // Seed the sink for this message: its entries are accumulated into a batch
      // and flushed below as one ParsedMsg tagged with this order_seq.
      sink_.order_seq_ = req->order_seq;
      sink_.ingress_qlen_ = req->qlen;

      bool is_channel_reset = false; // not expected for hot templates; ignored
      DataDecoder::decode_one(const_cast<char *>(req->msg), req->len, req->ts,
                              req->msg_seq, req->sending_time, &sink_,
                              is_channel_reset, /*debug=*/false);

      // Send this message's entries (one batch, tagged order_seq) to the Reconstructor.
      sink_.flush();

      // Signal the coordinator we are done reading this packet buffer. Sent AFTER
      // all ParsedEntry sends above, so the refcount only drops once the buffer
      // read is complete. reply() routes to the DecodeReq's sender (coordinator).
      reply(new msg::DecodeDone(req->parent_id));
    }

    DecodeSink sink_;
    char       name_[256];
  };
}
