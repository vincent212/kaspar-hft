#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "chutil/Time.hpp"
#include "logger/act/Logger.hpp"
#include "mdp3/StageHist.hpp"
#include "mdp3/mbo_if.hpp"
#include "mdp3/msg/DecodeDone.hpp"
#include "mdp3/msg/DecodedPacket.hpp"
#include "mdp3/msg/DecoderCmd.hpp"

#include <algorithm>
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
        // Stage timestamps, epoch ns. t0 = socket read; t_recv = arrival here.
        struct Timing
        {
            uint64_t t0, t_send, t_msgbuf, t_dispatch, t_wstart, t_wend, t_recv;
        };

        struct Pending
        {
            uint32_t epoch;
            uint32_t sn;
            uint32_t qlen;
            bool rc;
            bool is_channel_reset;
            Timing tm;
            std::unique_ptr<char[]> data;
            std::vector<RecordedCall> calls;
        };

        enum Stage { A1_READ_TO_SEND, A2_TO_MSGBUF, A3_MSGBUF_TO_DISPATCH,
                     A_SOCK_TO_DISPATCH, B_TO_WORKER, C_DECODE, D_TO_HANDLER,
                     E_REORDER_WAIT, F_REPLAY, TOTAL, NSTAGES };

        void record(const Timing &tm, uint64_t t_as, uint64_t t_ae)
        {
            if (!tm.t0)
                return;
            if (tm.t_send && tm.t_msgbuf)
            {
                stages_.h[A1_READ_TO_SEND].add(int64_t(tm.t_send - tm.t0));
                stages_.h[A2_TO_MSGBUF].add(int64_t(tm.t_msgbuf - tm.t_send));
                stages_.h[A3_MSGBUF_TO_DISPATCH].add(int64_t(tm.t_dispatch - tm.t_msgbuf));
            }
            stages_.h[A_SOCK_TO_DISPATCH].add(int64_t(tm.t_dispatch - tm.t0));
            stages_.h[B_TO_WORKER].add(int64_t(tm.t_wstart - tm.t_dispatch));
            stages_.h[C_DECODE].add(int64_t(tm.t_wend - tm.t_wstart));
            stages_.h[D_TO_HANDLER].add(int64_t(tm.t_recv - tm.t_wend));
            stages_.h[E_REORDER_WAIT].add(int64_t(t_as - tm.t_recv));
            stages_.h[F_REPLAY].add(int64_t(t_ae - t_as));
            stages_.h[TOTAL].add(int64_t(t_ae - tm.t0));
            static const char *const names[NSTAGES] = {"A1_read_to_send", "A2_to_msgbuf",
                "A3_msgbuf_to_dispatch", "A_sock_to_dispatch", "B_to_worker", "C_decode",
                "D_to_handler", "E_reorder_wait", "F_replay", "TOTAL_t0_to_replayed"};
            stages_.maybe_flush(t_ae, name_, names);
        }

        void on_decoded(const msg::DecodedPacket *m) noexcept
        {
            const Timing tm{m->t0, m->t_send, m->t_msgbuf, m->t_dispatch, m->t_wstart, m->t_wend, chutil::Time::epoch()};
            if (m->dispatch_id < next_)
            {
                log_err("%s duplicate or stale dispatch_id %lu (next %lu) sn %u -- ignored",
                        name_, m->dispatch_id, next_, m->sn);
                return;
            }
            if (m->dispatch_id == next_)
            {
                apply(m->dispatch_id, m->epoch, m->sn, m->qlen, m->rc,
                      m->is_channel_reset, m->calls, tm);
                ++next_;
                drain();
                return;
            }
            pending_.emplace(m->dispatch_id,
                             Pending{m->epoch, m->sn, m->qlen, m->rc,
                                     m->is_channel_reset, tm, std::move(m->data),
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
                      p.calls, p.tm);
            }
        }

        void apply(uint64_t id, uint32_t epoch, uint32_t sn, uint32_t qlen, bool rc,
                   bool is_channel_reset, std::vector<RecordedCall> &calls,
                   const Timing &tm)
        {
            if (have_dead_epoch_ && epoch <= dead_epoch_)
            {
                if (mp_)
                    mp_->send(new msg::DecodeDone(id, sn, true, false, false), this);
                return;
            }

            const uint64_t t_as = chutil::Time::epoch();
            cb_->set_ingress_qlen(qlen);
            for (auto &c : calls)
                c(*cb_);
            record(tm, t_as, chutil::Time::epoch());

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
        StageSet<NSTAGES> stages_;
        char name_[256];
    };
}
