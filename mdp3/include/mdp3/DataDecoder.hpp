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
#include "mdp3/msg/TriggerRecovery.hpp"
#include "mdp3/msg/DecodePacket.hpp"
#include "mdp3/msg/DecodeResult.hpp"
#include "mdp3/msg/DecoderCmd.hpp"

#include "logger/act/Logger.hpp"

#define TRACEF std::cerr

// decode_one is static so it cannot call the non-static get_name() that
// log_err/log_wrn use. Log with a literal component name instead.
#define SLOG_ERR(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_ERROR_, "DataDecoder", __FILE__, __LINE__, __VA_ARGS__)
#define SLOG_WRN(...) polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_WARN_, "DataDecoder", __FILE__, __LINE__, __VA_ARGS__)

namespace mdp3
{

    // The decode ACTOR. MessageProcessor fast_sends a DecodePacket per in-order
    // packet; this decodes it inline via mbo_data and replies DecodeResult{rc,is_channel_reset}.
    class DataDecoder : public actors::Actor
    {
    public:
        // chan names the actor. Actor names must be unique across the whole
        // process, and the old constant "DataDecoder" was not: a second channel
        // tripped Manager's "actor with this name already managed".
        // Defaulted so non-kaspr callers are unchanged.
        DataDecoder(
            feed_handler_if *_cb,
            bool _disable_mbo,
            uint32_t _max_mbp_level,
            bool _debug,
            uint32_t _chan = 0)
            : cb(_cb), debug(_debug)
        {
            snprintf(name_, sizeof(name_), "DataDecoder_%u", _chan);
            cb->set_max_mbp_level(_max_mbp_level);
            cb->disable_mbo(_disable_mbo);
            MESSAGE_HANDLER(msg::DecodePacket, on_decode_packet);
            MESSAGE_HANDLER(msg::DecoderCmd, on_cmd);
        }

        const char *get_name() const override { return name_; }

        // Decode ONE SBE message at `msg` (points at its 10-byte SBE header) into
        // `cb`. Returns false if a CRITICAL message failed to decode (caller must
        // trigger recovery), true otherwise; sets is_channel_reset on a reset.
        //
        // Static + cb/debug params so the per-message decode can be isolated from
        // DataDecoder's member state. Per-PACKET work (EndOfPacket) stays in mbo_data;
        // this handles exactly one message.
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

            // Decode every message in order via decode_one.
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
        // Decode one packet fast_sent by MessageProcessor. Reply carries rc + is_channel_reset.
        void on_decode_packet(const msg::DecodePacket *m) noexcept
        {
            cb->set_ingress_qlen(m->qlen);
            bool is_channel_reset = false;
            bool rc = mbo_data(const_cast<char *>(m->data), m->len, m->ts, is_channel_reset);
            reply(new msg::DecodeResult(rc, is_channel_reset));
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
    };
}