#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <iostream>
#include "mdp3/msg_decoder.hpp"

#include "logger/act/Logger.hpp"

namespace mdp3
{

    struct DataDecoder
    {

        const char* get_name() const { return "DataDecoder"; }

        feed_handler_if *cb;
        bool debug;
        bool version_warning = false;


#define TRACEF std::cerr

        DataDecoder(
            feed_handler_if *_cb,
            bool _disable_mbo,
            uint32_t _max_mbp_level,
            bool _debug) 
            : cb(_cb), debug(_debug)
        {
            cb->set_max_mbp_level(_max_mbp_level);
            cb->disable_mbo(_disable_mbo);
        }

        bool mbo_data(char *databuf, std::size_t len, uint64_t ts, bool &is_channel_reset)
        {
            auto data_start = databuf;

            const std::size_t sbe_message_header_size = 10;

            auto MsgSeqNum = *(unsigned int *)(databuf);
            databuf += sizeof(MsgSeqNum);

            auto SendingTime = *(unsigned long long *)(databuf);
            databuf += sizeof(SendingTime);

            if (debug)
            {
                TRACEF << "DECODING SEQ: " << MsgSeqNum << " len: " << len << std::endl;
            }

            while (std::size_t(databuf - data_start) < len)
            {

                auto MsgSize = *(unsigned short *)(databuf);
                databuf += sizeof(MsgSize);

                auto BlockLength = *(unsigned short *)(databuf);
                databuf += sizeof(BlockLength);

                auto TemplateID = *(unsigned short *)(databuf);
                databuf += sizeof(TemplateID);

                auto SchemaID = *(unsigned short *)(databuf);
                databuf += sizeof(SchemaID);

                auto Version = *(unsigned short *)(databuf);
                databuf += sizeof(Version);

                ASSERT(SchemaID == 1, "wrong schema id");
                //ASSERT(Version <= 12, "wrong version, must be <= 12");
                if (Version > 12 && !version_warning)
                {
                    log_wrn("wrong version, should be <= 12, version: %d", Version);
                    version_warning = true;
                }

                databuf -= sbe_message_header_size;

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
                        log_err("caught exception in decode_MDIncrementalRefreshBook46 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_MDIncrementalRefreshTradeSummary48 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_MDIncrementalRefreshDailyStatistics49 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_MDIncrementalRefreshSessionStatistics51 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_MDInstrumentDefinitionSpread56 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_MDInstrumentDefinitionOption55 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_MDInstrumentDefinitionFuture54 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_MDInstrumentDefinitionFixedIncome57 %s", std::string(e.what()));
                    }

                    break;
                }
                case sbe::ChannelReset4::sbeTemplateId():
                {
                    sbe::ChannelReset4 reset;
                    reset.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                    log_wrn("ChannelReset4");

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
                        log_err("caught exception in decode_ChannelReset4 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_ChannelReset4 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_SecurityStatus30 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_MDIncrementalRefreshOrderBook47 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_MDIncrementalRefreshVolume37 %s", std::string(e.what()));
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
                        log_err("caught exception in decode_QuoteRequest39 %s", std::string(e.what()));
                    }

                    break;
                }
                default:
                {
                    log_err("unknown message: %d", TemplateID);
                    if (debug)
                        TRACEF << "UNKNONWN MESSAGE " << TemplateID << std::endl;
                    break;
                }
                }

                databuf += MsgSize;

            }

            cb->EndOfPacket(MsgSeqNum, SendingTime);
            return true;
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
    };
}