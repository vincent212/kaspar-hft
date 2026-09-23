#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <iostream>
#include <map>
#include <cstring>
#include <boost/format.hpp>
#include "mdp3/msg_decoder.hpp"
#include "actors/Actor.hpp"
#include "chutil/Assert.hpp"
#include "mcast_recv/message_buffer.hpp"
#include "mdp3/msg/DecodeReq.hpp"
#include "mdp3/msg/DecodeDone.hpp"
#include "mdp3/msg/DecodePacket.hpp"
#include "mdp3/msg/DecodeResult.hpp"
#include "mdp3/msg/DecoderCmd.hpp"

#include "logger/act/Logger.hpp"

#define TRACEF std::cerr

// decode_one is static (shared by the inline path and the workers), so it cannot
// call the non-static get_name() that log_err/log_wrn use. Log with a literal
// component name instead.
#define SLOG_ERR(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_ERROR_, "DataDecoder", __FILE__, __LINE__, __VA_ARGS__)
#define SLOG_WRN(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_WARN_, "DataDecoder", __FILE__, __LINE__, __VA_ARGS__)

namespace mdp3
{

    // The decode ACTOR. MessageProcessor fast_sends a DecodePacket per in-order
    // packet; this decides parallel (all-hot -> fan out to the DecodeWorker fleet)
    // vs inline (cold packet -> mbo_data), and replies DecodeResult{rc,is_channel_reset}.
    // It owns the parallel coordination: packet-buffer slots kept alive until every
    // worker reports DecodeDone (refcount), and the global order_seq counter.
    //
    // Concurrency: MessageProcessor's inline fast_send-decode and this actor's own-
    // thread DecodeDone handling are serialized by the framework's fast_send_mutex
    // (both fast_send and process_message_internal take it), so pending_/order_seq_
    // need no extra lock.
    class DataDecoder : public actors::Actor
    {
    public:
        DataDecoder(
            feed_handler_if *_cb,
            bool _disable_mbo,
            uint32_t _max_mbp_level,
            bool _debug)
            : cb(_cb), debug(_debug)
        {
            snprintf(name_, sizeof(name_), "DataDecoder");
            cb->set_max_mbp_level(_max_mbp_level);
            cb->disable_mbo(_disable_mbo);
            MESSAGE_HANDLER(msg::DecodePacket, on_decode_packet);
            MESSAGE_HANDLER(msg::DecodeDone, on_decode_done);
            MESSAGE_HANDLER(msg::DecoderCmd, on_cmd);
        }

        const char *get_name() const override { return name_; }

        // Wire the warm decode-worker fleet (nworkers MUST be a power of two).
        // Until called, decode stays fully inline (parallel_decode_ off).
        void set_workers(actor_ptr *workers, uint32_t nworkers) noexcept
        {
            workers_ = workers;
            worker_mask_ = nworkers - 1;
            parallel_decode_ = (workers != nullptr && nworkers > 0);
        }

        // Templates a DecodeWorker can parse in parallel (stateless build). This
        // is the SINGLE source of truth for "hot": it MUST match DecodeWorker's
        // switch cases. Widen both together after measuring packet purity.
        static bool is_hot_template(uint16_t tid) noexcept
        {
            return tid == sbe::MDIncrementalRefreshBook46::sbeTemplateId()      // MBP & MBO book
                || tid == sbe::MDIncrementalRefreshOrderBook47::sbeTemplateId() // MBO book
                || tid == sbe::MDIncrementalRefreshTradeSummary48::sbeTemplateId(); // MBO trade
        }

        struct scan_result { uint32_t count; bool all_hot; bool has_hot; };

        // Walk the packet's SBE messages WITHOUT decoding: count them and report
        // whether EVERY one is hot (all_hot -> parallelizable) and whether ANY is
        // hot (has_hot -> used to assert the "no mixed hot+cold packet" assumption).
        // Cheap -- one u16 read per message.
        scan_result scan(const char *databuf, std::size_t len) const noexcept
        {
            const char *p = databuf + 12; // skip MsgSeqNum(4) + SendingTime(8)
            const char *const end = databuf + len;
            uint32_t count = 0;
            bool all_hot = true;
            bool has_hot = false;
            while (p < end)
            {
                const uint16_t MsgSize    = *reinterpret_cast<const uint16_t *>(p);
                const uint16_t TemplateID = *reinterpret_cast<const uint16_t *>(p + 4);
                if (is_hot_template(TemplateID))
                    has_hot = true;
                else
                    all_hot = false;
                p += MsgSize;
                ++count;
            }
            return {count, all_hot, has_hot};
        }

        // The dispatch loop: hand each SBE message to a warm worker (round-robin),
        // zero-copy (DecodeReq points into databuf). Stamps a global-monotonic
        // order_seq per message and the parent_id for the buffer refcount. Sender
        // is the coordinator, so workers reply DecodeDone there. Returns the count
        // dispatched. Caller must have verified scan().all_hot (else a worker asserts).
        // nworkers must be a power of two; pass worker_mask = nworkers - 1.
        uint32_t dispatch(const char *databuf, std::size_t len, uint64_t ts,
                          uint64_t order_seq_base, uint64_t parent_id,
                          actor_ptr *workers, uint32_t worker_mask,
                          actor_ptr coordinator, uint32_t qlen) const noexcept
        {
            const uint32_t msgSeqNum   = *reinterpret_cast<const uint32_t *>(databuf);
            const uint64_t sendingTime = *reinterpret_cast<const uint64_t *>(databuf + 4);
            const char *p = databuf + 12;
            const char *const end = databuf + len;
            uint32_t i = 0;
            while (p < end)
            {
                const uint16_t MsgSize = *reinterpret_cast<const uint16_t *>(p);
                workers[i & worker_mask]->send(
                    new msg::DecodeReq(p, MsgSize, msgSeqNum, ts, sendingTime, order_seq_base + i, parent_id, qlen),
                    coordinator);
                p += MsgSize;
                ++i;
            }
            return i;
        }

        // Decode ONE SBE message at `msg` (points at its 10-byte SBE header) into
        // `cb`. Returns false if a CRITICAL message failed to decode (caller must
        // trigger recovery), true otherwise; sets is_channel_reset on a reset.
        //
        // This is the SINGLE per-message decode, shared by mbo_data's loop (the
        // inline serial path, cb = handler_if) and DecodeWorker (the parallel
        // path, cb = a stateless DecodeSink). Static + cb/debug params so both a
        // MessageProcessor's DataDecoder and a worker can call it without owning
        // extra state. Per-PACKET work (EndOfPacket) stays in mbo_data / the
        // Reconstructor -- this handles exactly one message.
        static bool decode_one(char *msg, uint16_t MsgSize, uint64_t ts,
                               uint32_t MsgSeqNum, uint64_t SendingTime,
                               feed_handler_if *cb, bool &is_channel_reset, bool debug)
        {
            const std::size_t sbe_message_header_size = 10;
            char *databuf = msg;

            const uint16_t BlockLength = *(unsigned short *)(msg + 2);
            const uint16_t TemplateID  = *(unsigned short *)(msg + 4);
            const uint16_t SchemaID    = *(unsigned short *)(msg + 6);
            const uint16_t Version     = *(unsigned short *)(msg + 8);

            ASSERT(SchemaID == 1, "wrong schema id");

            if (debug)
            {
                TRACEF << "-----------------------------------------------------------------------------\n";
                TRACEF << "MsgSize: " << MsgSize << " TemplateID: " << TemplateID << " SchemaID: " << SchemaID << " Version: " << Version << "\n";
            }

            switch (TemplateID)
            {
                case sbe::MDIncrementalRefreshBook46::sbeTemplateId(): // MBP & MBO
                {
                    sbe::MDIncrementalRefreshBook46 incr;
                    incr.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << incr << std::endl;
                    }

                    try
                    {
                        decode_MDIncrementalRefreshBook46(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            incr,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_MDIncrementalRefreshBook46 %s", std::string(e.what()));
                        return false;
                    }

                    break;
                }
                case sbe::MDIncrementalRefreshTradeSummary48::sbeTemplateId(): // MBO and MBP trade
                {

                    sbe::MDIncrementalRefreshTradeSummary48 trade;
                    trade.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << trade << std::endl;
                    }

                    try
                    {
                        decode_MDIncrementalRefreshTradeSummary48(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            trade,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_MDIncrementalRefreshTradeSummary48 %s", std::string(e.what()));
                        return false;
                    }

                    break;
                }
                case sbe::MDIncrementalRefreshDailyStatistics49::sbeTemplateId():
                {
                    sbe::MDIncrementalRefreshDailyStatistics49 stats;
                    stats.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << stats << std::endl;
                    }

                    try
                    {
                        decode_MDIncrementalRefreshDailyStatistics49(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            stats,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_MDIncrementalRefreshDailyStatistics49 %s", std::string(e.what()));
                    }

                    break;
                }
                case sbe::MDIncrementalRefreshSessionStatistics51::sbeTemplateId():
                {
                    sbe::MDIncrementalRefreshSessionStatistics51 stats;
                    stats.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << stats << std::endl;
                    }

                    try
                    {
                        decode_MDIncrementalRefreshSessionStatistics51(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            stats,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_MDIncrementalRefreshSessionStatistics51 %s", std::string(e.what()));
                    }

                    break;
                }
                case sbe::MDInstrumentDefinitionSpread56::sbeTemplateId():
                {
                    sbe::MDInstrumentDefinitionSpread56 def;
                    def.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << def << std::endl;
                    }

                    try
                    {
                        decode_MDInstrumentDefinitionSpread56(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            def,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_MDInstrumentDefinitionSpread56 %s", std::string(e.what()));
                    }

                    break;
                }
                case sbe::MDInstrumentDefinitionOption55::sbeTemplateId():
                {
                    sbe::MDInstrumentDefinitionOption55 def;
                    def.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << def << std::endl;
                    }

                    try
                    {
                        decode_MDInstrumentDefinitionOption55(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            def,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_MDInstrumentDefinitionOption55 %s", std::string(e.what()));
                    }

                    break;
                }
                case sbe::MDInstrumentDefinitionFuture54::sbeTemplateId():
                {
                    sbe::MDInstrumentDefinitionFuture54 def;
                    def.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << def << std::endl;
                    }

                    try
                    {
                        decode_MDInstrumentDefinitionFuture54(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            def,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_MDInstrumentDefinitionFuture54 %s", std::string(e.what()));
                    }

                    break;
                }
                case sbe::MDInstrumentDefinitionFixedIncome57::sbeTemplateId():
                {
                    sbe::MDInstrumentDefinitionFixedIncome57 def;
                    def.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << def << std::endl;
                    }

                    try
                    {
                        decode_MDInstrumentDefinitionFixedIncome57(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            def,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_MDInstrumentDefinitionFixedIncome57 %s", std::string(e.what()));
                    }

                    break;
                }
                case sbe::ChannelReset4::sbeTemplateId():
                {
                    sbe::ChannelReset4 reset;
                    reset.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    SLOG_WRN("ChannelReset4");

                    is_channel_reset = true;

                    if (debug)
                    {
                        TRACEF << reset << std::endl;
                    }

                    try
                    {
                        decode_ChannelReset4(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            reset,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_ChannelReset4 %s", std::string(e.what()));
                        return false;
                    }

                    break;
                }
                case sbe::MDIncrementalRefreshLimitsBanding50::sbeTemplateId():
                {
                    sbe::MDIncrementalRefreshLimitsBanding50 limits;
                    limits.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << limits << std::endl;
                    }

                    try
                    {
                        decode_MDIncrementalRefreshLimitsBanding50(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            limits,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_ChannelReset4 %s", std::string(e.what()));
                    }

                    break;
                }
                case sbe::SecurityStatus30::sbeTemplateId():
                {
                    sbe::SecurityStatus30 status;
                    status.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << status << std::endl;
                    }

                    try
                    {
                        decode_SecurityStatus30(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            status,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_SecurityStatus30 %s", std::string(e.what()));
                    }

                    break;
                }
                case sbe::MDIncrementalRefreshOrderBook47::sbeTemplateId(): // MBO
                {
                    sbe::MDIncrementalRefreshOrderBook47 incr;
                    incr.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << incr << std::endl;
                    }

                    try
                    {
                        decode_MDIncrementalRefreshOrderBook47(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            incr,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_MDIncrementalRefreshOrderBook47 %s", std::string(e.what()));
                        return false;
                    }

                    break;
                }
                case sbe::MDIncrementalRefreshVolume37::sbeTemplateId():
                {
                    sbe::MDIncrementalRefreshVolume37 vol;
                    vol.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << vol << std::endl;
                    }

                    try
                    {
                        decode_MDIncrementalRefreshVolume37(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            vol,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_MDIncrementalRefreshVolume37 %s", std::string(e.what()));
                    }

                    break;
                }
                case sbe::AdminHeartbeat12::sbeTemplateId():
                {
                    // sbe::AdminHeartbeat12 beat;
                    break;
                }
                case sbe::QuoteRequest39::sbeTemplateId():
                {
                    sbe::QuoteRequest39 rfq;
                    rfq.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    if (debug)
                    {
                        TRACEF << rfq << std::endl;
                    }

                    try
                    {
                        decode_QuoteRequest39(
                            ts,
                            MsgSeqNum,
                            SendingTime,
                            rfq,
                            cb);
                    }
                    catch (std::runtime_error &e)
                    {
                        SLOG_ERR("caught exception in decode_QuoteRequest39 %s", std::string(e.what()));
                    }

                    break;
                }
                default:
                {
                    SLOG_ERR("unknown message: %d", TemplateID);
                    if (debug)
                        TRACEF << "UNKNONWN MESSAGE " << TemplateID << std::endl;
                    break;
                }
            }

            return true;
        }

        bool mbo_data(char *databuf, std::size_t len, uint64_t ts, bool &is_channel_reset)
        {
            auto data_start = databuf;

            auto MsgSeqNum = *(unsigned int *)(databuf);
            databuf += sizeof(MsgSeqNum);

            auto SendingTime = *(unsigned long long *)(databuf);
            databuf += sizeof(SendingTime);

            if (debug)
                TRACEF << "DECODING SEQ: " << MsgSeqNum << " len: " << len << std::endl;

            // Inline serial path: decode every message in order via the shared
            // per-message decode_one (same code the parallel workers run).
            while (std::size_t(databuf - data_start) < len)
            {
                const uint16_t MsgSize = *(unsigned short *)(databuf);
                if (!decode_one(databuf, MsgSize, ts, MsgSeqNum, SendingTime, cb, is_channel_reset, debug))
                    return false;
                databuf += MsgSize;
            }

            cb->EndOfPacket(MsgSeqNum, SendingTime);
            return true;
        }

        // Hand the handler the ingress mailbox depth for the packet that is
        // about to be decoded. Call immediately before mbo_data(); it applies
        // to every callback that decode fires.
        void set_ingress_qlen(uint32_t qlen)
        {
            cb->set_ingress_qlen(qlen);
        }

        void
        gap()
        {
            cb->Gap();
        }

        void burstend(uint32_t cnt = 1)
        {
            cb->BurstEnd(cnt);
        }

        void print_stats()
        {
            cb->PrintStats();
        }

    private:
        // Decode one packet fast_sent by MessageProcessor: parallel if all-hot,
        // else inline. Reply carries rc + is_channel_reset for the caller.
        void on_decode_packet(const msg::DecodePacket *m) noexcept
        {
            const auto sr = scan(m->data, m->len);

            // All-hot packet: fan out to the worker fleet. Copy the packet into a
            // DataDecoder-owned slot (kept alive until every worker reports
            // DecodeDone), dispatch zero-copy DecodeReqs into that slot, reply now.
            if (parallel_decode_ && sr.count > 0 && sr.all_hot)
            {
                const uint64_t pid = next_parent_id_++;
                const uint64_t base = order_seq_;
                order_seq_ += sr.count;
                auto &pp = pending_[pid];
                std::memcpy(pp.buf.message.data(), m->data, m->len);
                pp.buf.len = m->len;
                pp.outstanding = sr.count;
                // The packet's ingress depth rides each DecodeReq to the worker
                // that decodes it. It CANNOT go through cb->set_ingress_qlen()
                // here: that writes one member on the shared handler, which the
                // worker threads read while decoding other packets.
                dispatch(pp.buf.message.data(), m->len, m->ts, base, pid,
                         workers_, worker_mask_, this, m->qlen);
                reply(new msg::DecodeResult(true, false)); // hot packets never channel-reset
                return;
            }

            // Not all-hot. ASSUME a packet never mixes hot (book/trade) with cold
            // templates; if it did, book/trade would take this inline path and split
            // the orderid map from the Reconstructor's. Trip if that is violated.
            ASSERTF(!sr.has_hot, boost::format(
                "mixed hot+cold packet: book/trade on the inline path splits the "
                "orderid map -- parallel-decode assumption violated"));

            // Cold packet (or parallel off): decode inline. Serial, single
            // thread, so the handler member is the right place for the depth --
            // this is the same call main makes from MessageProcessor.
            cb->set_ingress_qlen(m->qlen);
            bool is_channel_reset = false;
            bool rc = mbo_data(const_cast<char *>(m->data), m->len, m->ts, is_channel_reset);
            reply(new msg::DecodeResult(rc, is_channel_reset));
        }

        // A worker finished reading a packet's slot. Drop the refcount; free the
        // slot at zero. Runs on THIS actor's thread, serialized against the inline
        // fast_send-decode above by the framework's fast_send_mutex.
        void on_decode_done(const msg::DecodeDone *d) noexcept
        {
            auto it = pending_.find(d->parent_id);
            if (it == pending_.end())
                return;
            if (--it->second.outstanding == 0)
                pending_.erase(it); // frees the packet-buffer slot
        }

        // Low-rate control ops from MessageProcessor (gap / burstend / print_stats).
        void on_cmd(const msg::DecoderCmd *m) noexcept
        {
            switch (m->kind)
            {
            case msg::DecoderCmd::GAP:        gap();            break;
            case msg::DecoderCmd::BURSTEND:   burstend(m->cnt); break;
            case msg::DecoderCmd::PRINTSTATS: print_stats();    break;
            }
        }

        feed_handler_if *cb;
        bool debug;
        char name_[256];

        // ---- parallel coordination ----
        actor_ptr *workers_ = nullptr;      // warm decode workers (set via set_workers)
        uint32_t   worker_mask_ = 0;        // nworkers - 1 (power of two)
        bool       parallel_decode_ = false;
        uint64_t   order_seq_ = 0;          // global monotonic key stamped per message
        uint64_t   next_parent_id_ = 1;

        // A packet handed to workers: its bytes (kept alive for zero-copy reads)
        // plus the count of workers still to report DecodeDone.
        struct pending_packet
        {
            mcast_recv::message_buffer buf;
            uint32_t outstanding;
        };
        std::map<uint64_t, pending_packet> pending_;
    };
}