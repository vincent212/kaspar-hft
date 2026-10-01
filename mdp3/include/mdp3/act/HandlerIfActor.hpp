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

        // Fixed-bucket latency histogram: 100 ns bins to 50 us, 1 us bins to
        // 2 ms, then one overflow bin. O(1) to record, cheap to summarize.
        struct Hist
        {
            static constexpr uint32_t kFine = 500, kCoarse = 1950, kBins = kFine + kCoarse + 1;
            std::vector<uint64_t> bins = std::vector<uint64_t>(kBins, 0);
            uint64_t n = 0, max = 0;

            void add(int64_t ns)
            {
                if (ns < 0) ns = 0;
                uint64_t v = uint64_t(ns);
                uint32_t b = v < 50000 ? uint32_t(v / 100)
                           : v < 2000000 ? kFine + uint32_t((v - 50000) / 1000)
                           : kBins - 1;
                ++bins[b];
                ++n;
                if (v > max) max = v;
            }
            // Upper edge of the bin holding quantile q, in us.
            double q(double p) const
            {
                if (!n) return 0;
                uint64_t want = uint64_t(p * double(n - 1)) + 1, c = 0;
                for (uint32_t b = 0; b < kBins; ++b)
                    if ((c += bins[b]) >= want)
                        return b < kFine ? (b + 1) * 0.1
                             : b < kFine + kCoarse ? 50.0 + (b - kFine + 1)
                             : double(max) / 1000.0;
                return double(max) / 1000.0;
            }
            void clear() { std::fill(bins.begin(), bins.end(), 0); n = max = 0; }
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
                hist_[A1_READ_TO_SEND].add(int64_t(tm.t_send - tm.t0));
                hist_[A2_TO_MSGBUF].add(int64_t(tm.t_msgbuf - tm.t_send));
                hist_[A3_MSGBUF_TO_DISPATCH].add(int64_t(tm.t_dispatch - tm.t_msgbuf));
            }
            hist_[A_SOCK_TO_DISPATCH].add(int64_t(tm.t_dispatch - tm.t0));
            hist_[B_TO_WORKER].add(int64_t(tm.t_wstart - tm.t_dispatch));
            hist_[C_DECODE].add(int64_t(tm.t_wend - tm.t_wstart));
            hist_[D_TO_HANDLER].add(int64_t(tm.t_recv - tm.t_wend));
            hist_[E_REORDER_WAIT].add(int64_t(t_as - tm.t_recv));
            hist_[F_REPLAY].add(int64_t(t_ae - t_as));
            hist_[TOTAL].add(int64_t(t_ae - tm.t0));
            if (!last_flush_)
                last_flush_ = t_ae;
            else if (t_ae - last_flush_ > 10000000000ULL)
            {
                static const char *names[NSTAGES] = {"A1_read_to_send", "A2_to_msgbuf",
                    "A3_msgbuf_to_dispatch", "A_sock_to_dispatch", "B_to_worker", "C_decode",
                    "D_to_handler", "E_reorder_wait", "F_replay", "TOTAL_t0_to_replayed"};
                for (int s = 0; s < NSTAGES; ++s)
                    log_inf("%s STAGE %s n=%lu p1=%.1f p10=%.1f p50=%.1f p90=%.1f p99=%.1f p999=%.1f max=%.1f us",
                            name_, names[s], hist_[s].n, hist_[s].q(.01), hist_[s].q(.10),
                            hist_[s].q(.5), hist_[s].q(.9), hist_[s].q(.99), hist_[s].q(.999),
                            double(hist_[s].max) / 1000.0);
                for (auto &h : hist_)
                    h.clear();
                last_flush_ = t_ae;
            }
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
        Hist hist_[NSTAGES];
        uint64_t last_flush_ = 0;
        char name_[256];
    };
}
