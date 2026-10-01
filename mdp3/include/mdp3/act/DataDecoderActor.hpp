#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "mdp3/DataDecoder.hpp"
#include "mdp3/RecordingHandler.hpp"
#include "mdp3/msg/DecodedPacket.hpp"
#include "mdp3/msg/ParDecodePacket.hpp"

#include <cstdio>

namespace mdp3
{
    // One parallel decode worker. Runs the unchanged serial decoder
    // (DataDecoder::mbo_data) against a RecordingHandler, so every SBE template
    // the serial path handles is handled identically here, and forwards one
    // DecodedPacket per packet to the HandlerIfActor.
    class DataDecoderActor : public actors::Actor
    {
    public:
        // spin > 0: busy-poll the mailbox that many times before parking, so a
        // packet does not wait on a futex wakeup of this thread.
        DataDecoderActor(uint32_t chan, uint32_t worker, actor_ptr handler_actor,
                         bool disable_mbo, uint32_t max_mbp_level, size_t spin = 0)
            : handler_actor_(handler_actor),
              dec_(&rec_, disable_mbo, max_mbp_level, /*debug=*/false, chan)
        {
            if (spin)
                set_mailbox(MailboxKind::LockFreeMPSC, 0, spin);
            snprintf(name_, sizeof(name_), "DataDecoderActor_%u_%u", chan, worker);
            MESSAGE_HANDLER(msg::ParDecodePacket, on_packet);
        }

        const char *get_name() const override { return name_; }

    private:
        void on_packet(const msg::ParDecodePacket *m) noexcept
        {
            auto *d = new msg::DecodedPacket();
            d->dispatch_id = m->dispatch_id;
            d->epoch = m->epoch;
            d->sn = m->sn;
            d->qlen = m->qlen;
            d->data = std::move(m->data);

            rec_.out = &d->calls;
            bool is_channel_reset = false;
            d->rc = dec_.mbo_data(d->data.get(), m->len, m->ts, is_channel_reset);
            d->is_channel_reset = is_channel_reset;
            rec_.out = nullptr;

            handler_actor_->send(d, this);
        }

        actor_ptr handler_actor_;
        RecordingHandler rec_;
        DataDecoder dec_;
        char name_[256];
    };
}
