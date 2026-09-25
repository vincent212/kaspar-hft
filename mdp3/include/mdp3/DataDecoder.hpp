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
#include "mdp3/msg/TriggerRecovery.hpp"
#include "mdp3/msg/DecodePacket.hpp"
#include "mdp3/msg/DecodeResult.hpp"
#include "mdp3/msg/DecoderCmd.hpp"

#include "logger/act/Logger.hpp"

// The inline-bypass sink. Safe to include: DecodeSink.hpp pulls mbo_if.hpp and
// ParsedMsg.hpp, never this header, so there is no cycle.
#include "mdp3/DecodeSink.hpp"

#define TRACEF std::cerr

// decode_one is static (shared by the inline path and the workers), so it cannot
// call the non-static get_name() that log_err/log_wrn use. Log with a literal
// component name instead.
#define SLOG_ERR(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_ERROR_, "DataDecoder", __FILE__, __LINE__, __VA_ARGS__)
#define SLOG_WRN(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_WARN_, "DataDecoder", __FILE__, __LINE__, __VA_ARGS__)

namespace mdp3
{

    // The decode ACTOR. MessageProcessor fast_sends a DecodePacket per in-order
    // packet; this decides parallel (worker fleet on -> fan out every message) vs
    // inline (fleet off -> mbo_data), and replies DecodeResult{rc,is_channel_reset}.
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
        // path_tag / chan only name the actor. Actor names must be unique across
        // the whole process, and the old constant "DataDecoder" was not: a second
        // channel, or a second decode path on one channel, tripped Manager's
        // "actor with this name already managed". Convention is "_S" for the
        // serial (inline) path and "_P" for the parallel (worker-fleet) path,
        // e.g. DataDecoder_S_310. Defaulted so non-kaspr callers are unchanged.
        DataDecoder(
            feed_handler_if *_cb,
            bool _disable_mbo,
            uint32_t _max_mbp_level,
            bool _debug,
            const char *_path_tag = "S",
            uint32_t _chan = 0)
            : cb(_cb), debug(_debug)
        {
            snprintf(name_, sizeof(name_), "DataDecoder_%s_%u", _path_tag, _chan);
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
            // dispatch() round-robins with `i & worker_mask_`, which visits every
            // worker ONLY when nworkers is a power of two. A non-pow2 count routes
            // to a subset, so order_seq values reserved for the unreachable workers
            // are never produced -- the Reconstructor's expected_seq_ then stalls
            // permanently. Fail loudly at wiring time rather than hang at runtime.
            ASSERTF(nworkers == 0 || (nworkers & (nworkers - 1)) == 0,
                    boost::format("cme_decode_workers must be a power of two, got %u") % nworkers);
            workers_ = workers;
            // nworkers==0 is the serial build. `nworkers - 1` would wrap to
            // UINT32_MAX; harmless only for as long as nothing reads the mask
            // with parallel off, which is not a property worth relying on.
            worker_mask_ = nworkers ? (nworkers - 1) : 0;
            parallel_decode_ = (workers != nullptr && nworkers > 0);
        }

        // The actor (MessageProcessor) to notify when a parallel decode fails so it
        // can initiate recovery. Wired in kaspr once both actors exist.
        void set_recovery_target(actor_ptr mp) noexcept { recovery_target_ = mp; }

        // Wire the inline-bypass sink. Needed only on the parallel path: small
        // packets are decoded on this thread and their entries sent straight to the
        // Reconstructor, so this decoder needs its own sink (one, not per worker --
        // the bypass runs on the coordinator thread and is serialized with itself
        // by the framework's fast_send_mutex, the same guarantee on_decode_done
        // relies on). Without it the bypass is disabled and every packet fans out,
        // which is the pre-bypass behaviour.
        void set_inline_sink(DecodeSink *s) noexcept { inline_sink_ = s; }


        // Packets with at most this many SBE messages skip the worker fan-out and
        // are decoded inline. Sized from the feed: messages per packet are p99 2-4
        // and max 34 (1.31M CME packets, ES/NQ/ZN), so 8 takes essentially all
        // ordinary traffic inline while genuinely fat packets still parallelize.
        static constexpr uint32_t INLINE_MAX_MSGS = 8;

        // Decode every message of a packet on THIS thread, sending each message's
        // entries to the Reconstructor tagged with a dense order_seq -- byte for
        // byte what a DecodeWorker would have sent, minus the queue hops. Returns
        // the number of messages decoded, so the caller advances order_seq_ by
        // exactly the same amount dispatch() would have.
        // `ok` is set false if ANY message in the packet failed a critical decode.
        // The caller MUST propagate it: decode_one returns false when a book or
        // trade template throws, which means the book is now missing updates and
        // only recovery can resync it. Swallowing that diverges the book silently
        // until the next gap -- the failure mode the serial path's own comment
        // ("never silently swallow a malformed feed") exists to prevent.
        uint32_t decode_inline(const char *databuf, std::size_t len, uint64_t ts,
                               uint64_t order_seq_base, uint32_t qlen,
                               bool &ok) noexcept
        {
            const uint32_t msgSeqNum   = *reinterpret_cast<const uint32_t *>(databuf);
            const uint64_t sendingTime = *reinterpret_cast<const uint64_t *>(databuf + 4);
            const char *p = databuf + 12;
            const char *const end = databuf + len;
            uint32_t i = 0;
            ok = true;
            while (p < end)
            {
                // Framing already validated by count_messages()'s ASSERTFs on this
                // same packet -- which compile out under -DNOASSERT, so under that
                // flag neither walk validates anything. NOASSERT is defined nowhere
                // today; if that changes, this walk needs its own bounds check.
                const uint16_t MsgSize = *reinterpret_cast<const uint16_t *>(p);
                inline_sink_->seed(order_seq_base + i, qlen);
                bool is_channel_reset = false; // applied via the l3_chr_v2_t entry
                if (!decode_one(const_cast<char *>(p), MsgSize, ts, msgSeqNum, sendingTime,
                                inline_sink_, is_channel_reset, /*debug=*/false))
                    ok = false;
                // Flush regardless: entries built before the throw were already
                // applied, and the Reconstructor's expected_seq_ must advance for
                // every dispatched message or the stream stalls. Recovery is the
                // caller's job, via the returned `ok`.
                inline_sink_->flush();
                p += MsgSize;
                ++i;
            }
            return i;
        }

        // Count the SBE messages in a packet without decoding or sending any.
        // Same framing walk and the same asserts as dispatch(), so a packet that
        // would abort there aborts here, at the same offset, with the same text.
        // Used by the inline-bypass decision below: the fan-out is only worth its
        // coordination cost when there is real fan-out to be had.
        uint32_t count_messages(const char *databuf, std::size_t len) const noexcept
        {
            const uint32_t msgSeqNum = *reinterpret_cast<const uint32_t *>(databuf);
            const char *p = databuf + 12;
            const char *const end = databuf + len;
            uint32_t i = 0;
            while (p < end)
            {
                ASSERTF(p + sizeof(uint16_t) <= end,
                        boost::format("count_messages: truncated SBE length prefix at offset %ld of %zu (seq %u)")
                            % (p - databuf) % len % msgSeqNum);
                const uint16_t MsgSize = *reinterpret_cast<const uint16_t *>(p);
                ASSERTF(MsgSize >= 10 && p + MsgSize <= end,
                        boost::format("count_messages: malformed SBE frame, MsgSize=%u at offset %ld of %zu (seq %u)")
                            % MsgSize % (p - databuf) % len % msgSeqNum);
                p += MsgSize;
                ++i;
            }
            return i;
        }

        // The dispatch loop: hand each SBE message to a warm worker (round-robin),
        // zero-copy (DecodeReq points into databuf). Stamps a global-monotonic
        // order_seq per message and the parent_id for the buffer refcount. Sender
        // is the coordinator, so workers reply DecodeDone there. Returns the count
        // dispatched. Every message is dispatched regardless of template; the worker
        // decodes it and the Reconstructor applies whatever entries it produces.
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
                // CME packets are well-formed by construction: every frame carries
                // a non-zero MsgSize (>= the 10-byte length+SBE header) and the
                // frames tile the packet exactly. A zero or over-long MsgSize is
                // impossible on the wire -- it can only mean memory corruption or a
                // decode bug -- so fail loud here rather than silently truncate the
                // packet (matches RecoveryProcessor's ERR on the snapshot feed and
                // mbo_data's abort-and-recover on the serial path).
                //
                // Check the 2-byte length prefix fits BEFORE reading it, so a
                // truncated tail can never over-read past `end` into the prefix.
                ASSERTF(p + sizeof(uint16_t) <= end,
                        boost::format("dispatch: truncated SBE length prefix at offset %ld of %zu (seq %u)")
                            % (p - databuf) % len % msgSeqNum);
                const uint16_t MsgSize = *reinterpret_cast<const uint16_t *>(p);
                ASSERTF(MsgSize >= 10 && p + MsgSize <= end,
                        boost::format("dispatch: malformed SBE frame, MsgSize=%u at offset %ld of %zu (seq %u)")
                            % MsgSize % (p - databuf) % len % msgSeqNum);
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
                if (MsgSize == 0)
                {
                    // Corrupt: a zero length can never advance the walk. Surface it
                    // and FAIL the packet so the caller initiates recovery -- never
                    // silently swallow a malformed feed.
                    log_err("corrupt SBE MsgSize==0 (seq %u) -- aborting packet, initiating recovery", MsgSeqNum);
                    return false;
                }
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
        // Decode one packet fast_sent by MessageProcessor: parallel if the worker
        // fleet is on, else inline. Reply carries rc + is_channel_reset.
        void on_decode_packet(const msg::DecodePacket *m) noexcept
        {
            // Parallel path: fan EVERY message out to the worker fleet with a dense
            // order_seq; the Reconstructor resequences and applies them all in wire
            // order (book, trade, definition, reset). A message a worker produces no
            // entries for (e.g. stats/volume) flushes an empty batch, which still
            // advances the Reconstructor's expected order_seq, so it never stalls.
            if (parallel_decode_)
            {
                // INLINE BYPASS. Fan-out only pays when there is something to fan
                // out. Measured over 1.31M CME packets (ES/NQ/ZN, 900s): messages
                // per packet mean 1.06-1.10, p50 1, p90 1, p99 2-4. So the common
                // packet dispatches ONE message to ONE worker and still pays the
                // full coordination bill -- a 1500-byte packet memcpy, a pending_
                // map insert/erase, a ParsedMsg send, a DecodeDone reply, and two
                // actor hops. That overhead is the ~7us gap measured between the
                // serial and parallel paths (NQ book p50 6.45us vs 13.78us).
                //
                // For a packet at or under INLINE_MAX_MSGS, decode it right here on
                // this thread -- no copy, no pending_ slot, no worker, no reply --
                // and hand the entries to the Reconstructor exactly as a worker
                // would, tagged with the same dense order_seq. Ordering is
                // therefore unchanged: the Reconstructor still applies strictly by
                // order_seq, so an inline packet and a fanned-out packet around it
                // cannot overtake each other.
                const uint32_t nmsg = inline_sink_ ? count_messages(m->data, m->len)
                                                  : UINT32_MAX;
                if (nmsg <= INLINE_MAX_MSGS)
                {
                    bool ok = true;
                    if (nmsg)
                    {
                        const uint32_t k = decode_inline(m->data, m->len, m->ts,
                                                         order_seq_, m->qlen, ok);
                        order_seq_ += k;
                    }
                    // Propagate a critical decode failure, exactly as the serial
                    // path does with mbo_data's return: MessageProcessor logs and
                    // calls do_data_recovery() on rc=false. The fan-out path reaches
                    // the same outcome the long way round, via DecodeDone.ok ->
                    // pp.failed -> TriggerRecovery. All three paths must agree; a
                    // book missing updates cannot be left to the next gap.
                    reply(new msg::DecodeResult(ok, false));
                    return;
                }

                const uint64_t pid  = next_parent_id_++;
                const uint64_t base = order_seq_;
                auto &pp = pending_[pid];
                std::memcpy(pp.buf.message.data(), m->data, m->len);
                pp.buf.len = m->len;
                pp.failed = false;
                // dispatch walks the packet, sends one DecodeReq per message
                // (order_seq = base + i, ingress depth riding each req), and returns
                // the count. Reserving order_seq_/outstanding AFTER is safe:
                // on_decode_done is serialized against this handler by the
                // framework's fast_send_mutex, so no worker completion is processed
                // until we return.
                const uint32_t n = dispatch(pp.buf.message.data(), m->len, m->ts, base, pid,
                                            workers_, worker_mask_, this, m->qlen);
                order_seq_ += n;
                pp.outstanding = n;
                if (n == 0)
                    pending_.erase(pid); // empty packet -> no worker -> no DecodeDone to free the slot
                // is_channel_reset stays false in the reply: a ChannelReset in the
                // packet is applied by the Reconstructor (l3_chr_v2_t -> clear the
                // orderid map) in wire order, not signalled back through here.
                reply(new msg::DecodeResult(true, false));
                return;
            }

            // Parallel off (serial default): decode inline. Serial, single thread,
            // so the handler member is the right place for the depth -- this is the
            // same call main makes from MessageProcessor.
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
            if (!d->ok)
                it->second.failed = true; // a worker's decode_one failed on a message
            if (--it->second.outstanding == 0)
            {
                const bool failed = it->second.failed;
                pending_.erase(it); // frees the packet-buffer slot
                // A message failed to decode -> the book is now missing
                // updates. Ask MessageProcessor to initiate recovery -- the same
                // response the inline path gives an mbo_data failure -- so the book
                // resyncs instead of silently diverging.
                if (failed && recovery_target_)
                    recovery_target_->send(new msg::TriggerRecovery(), this);
            }
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
            bool     failed; // set if any worker reported a decode failure
        };
        std::map<uint64_t, pending_packet> pending_;
        actor_ptr recovery_target_ = nullptr; // MessageProcessor; asked to recover on decode failure
        // Inline-bypass sink (parallel path only). Null => bypass disabled, every
        // packet fans out exactly as before.
        DecodeSink *inline_sink_ = nullptr;
    };
}