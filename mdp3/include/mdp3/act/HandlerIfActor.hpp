#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "logger/act/Logger.hpp"
#include "mdp3/mbo_if.hpp"
#include "mdp3/msg/DecodeDone.hpp"
#include "mdp3/msg/DecodedPacket.hpp"
#include "mdp3/msg/DecoderCmd.hpp"

#include <cstdio>
#include <map>
#include <memory>
#include <vector>

namespace mdp3
{
    // The single owner of handler_if while parallel decode is live. Receives one
    // DecodedPacket per dispatched packet from N DataDecoderActors, in any order,
    // and replays them into handler_if strictly in dispatch_id order -- the same
    // order the serial path would have decoded them in. Acknowledges each packet
    // to MessageProcessor with a DecodeDone, also in dispatch order.
    //
    // RecoveryProcessor still calls handler_if from its own thread. That is only
    // safe because MessageProcessor does not start a recovery until every
    // dispatched packet has been acknowledged here.
    class HandlerIfActor : public actors::Actor
    {
    public:
        // spin > 0: busy-poll the mailbox before parking (see DataDecoderActor).
        HandlerIfActor(uint32_t chan, feed_handler_if *cb, size_t spin = 0) : cb_(cb)
        {
            if (spin)
                set_mailbox(MailboxKind::LockFreeMPSC, 0, spin);
            snprintf(name_, sizeof(name_), "HandlerIfActor_%u", chan);
            MESSAGE_HANDLER(msg::DecodedPacket, on_decoded);
            MESSAGE_HANDLER(msg::DecoderCmd, on_cmd);
        }

        const char *get_name() const override { return name_; }

        void set_message_processor(actor_ptr mp) { mp_ = mp; }

        uint64_t next_dispatch_id() const { return next_; }
        std::size_t num_buffered() const { return pending_.size(); }

    private:
        struct Pending
        {
            uint32_t epoch;
            uint32_t sn;
            uint32_t qlen;
            bool rc;
            bool is_channel_reset;
            std::unique_ptr<char[]> data;
            std::vector<RecordedCall> calls;
        };

        void on_decoded(const msg::DecodedPacket *m) noexcept
        {
            if (m->dispatch_id < next_)
            {
                log_err("%s duplicate or stale dispatch_id %lu (next %lu) sn %u -- ignored",
                        name_, m->dispatch_id, next_, m->sn);
                return;
            }
            if (m->dispatch_id == next_)
            {
                apply(m->dispatch_id, m->epoch, m->sn, m->qlen, m->rc,
                      m->is_channel_reset, m->calls);
                ++next_;
                drain();
                return;
            }
            pending_.emplace(m->dispatch_id,
                             Pending{m->epoch, m->sn, m->qlen, m->rc,
                                     m->is_channel_reset, std::move(m->data),
                                     std::move(m->calls)});
        }

        void drain()
        {
            for (auto it = pending_.begin();
                 it != pending_.end() && it->first == next_;
                 it = pending_.erase(it), ++next_)
            {
                auto &p = it->second;
                apply(it->first, p.epoch, p.sn, p.qlen, p.rc, p.is_channel_reset,
                      p.calls);
            }
        }

        void apply(uint64_t id, uint32_t epoch, uint32_t sn, uint32_t qlen, bool rc,
                   bool is_channel_reset, std::vector<RecordedCall> &calls)
        {
            if (have_dead_epoch_ && epoch <= dead_epoch_)
            {
                if (mp_)
                    mp_->send(new msg::DecodeDone(id, sn, true, false, false), this);
                return;
            }

            cb_->set_ingress_qlen(qlen);
            for (auto &c : calls)
                c(*cb_);

            if (!rc)
            {
                // Same as the serial path: callbacks decoded before the failure
                // have already been applied, then the handler is told of the gap.
                cb_->Gap();
                dead_epoch_ = epoch;
                have_dead_epoch_ = true;
            }

            if (mp_)
                mp_->send(new msg::DecodeDone(id, sn, rc, is_channel_reset, true), this);
        }

        // BURSTEND and PRINTSTATS arrive by fast_send from MessageProcessor and
        // run immediately, not in dispatch order. PRINTSTATS is a shutdown dump;
        // BURSTEND is compiled out (SENDEOBURST).
        void on_cmd(const msg::DecoderCmd *m) noexcept
        {
            switch (m->kind)
            {
            case msg::DecoderCmd::GAP:        cb_->Gap();            break;
            case msg::DecoderCmd::BURSTEND:   cb_->BurstEnd(m->cnt); break;
            case msg::DecoderCmd::PRINTSTATS: cb_->PrintStats();     break;
            }
        }

        feed_handler_if *cb_;
        actor_ptr mp_ = nullptr;
        uint64_t next_ = 0;
        std::map<uint64_t, Pending> pending_;
        uint32_t dead_epoch_ = 0;
        bool have_dead_epoch_ = false;
        char name_[256];
    };
}
