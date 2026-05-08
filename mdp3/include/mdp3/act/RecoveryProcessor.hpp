
#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Continue.hpp"
#include "chutil/udp_socket.hpp"

#include "mktdata_v12/SnapshotFullRefreshOrderBook53.h"

#include "mdp3/msg_decoder.hpp"

#include "logger/act/Logger.hpp"
#include "mcast_recv/message_buffer.hpp"
#include "mdp3/mbo_if.hpp"

#include "mdp3/msg/DoDataRecovery.hpp"
#include "mdp3/msg/DoInstrumentRecovery.hpp"
#include "mdp3/msg/EndDataRecovery.hpp"
#include "mdp3/msg/EndInstrumentRecovery.hpp"

namespace mdp3
{

    class RecoveryProcessor : public actors::Actor
    {

        const char* get_name() const { return cached_name; }
        char cached_name[256];

    public:
        RecoveryProcessor(
            const std::string _chan_nam,
            feed_handler_if *_cb,
            in_port_t _port_dr,
            in_port_t _port_ir,
            const char *_group_dr,
            const char *_group_ir,
            const char *_interface,
            bool _debug) : cb(_cb),
                           port_dr(_port_dr),
                           port_ir(_port_ir),
                           group_dr(_group_dr),
                           group_ir(_group_ir),
                           interface(_interface),
                           debug(_debug)
        {
            snprintf(cached_name, sizeof(cached_name), "%sRecoveryProcessor", _chan_nam.c_str());
            MESSAGE_HANDLER(actors::msg::Start, start_handler);
            MESSAGE_HANDLER(msg::DoDataRecovery, datarecovery_handler);
            MESSAGE_HANDLER(msg::DoInstrumentRecovery, instrumentrecovery_handler);
        }

    private:
        bool debug;
        in_port_t port_ir, port_dr;
        int sock_ir, sock_dr;
        struct sockaddr_in addr_ir, addr_dr;
        std::size_t addrlen_ir, addrlen_dr;
        std::string group_ir, group_dr, interface;
        feed_handler_if *cb;

        mcast_recv:: message_buffer *read_dr() const noexcept
        {
            auto m = new mcast_recv::message_buffer();
            auto nrec = chutil::mcast::receive(sock_dr, &m->message[0], mcast_recv::msgsz, addr_dr, addrlen_dr);
            if (nrec == 0)
            {
                delete m;
                m=nullptr;
                return 0;
            }
            m->len = nrec;
            auto seq_num = *(unsigned int *)(&m->message[0]);
            m->seqnum = seq_num;
            return m;
        }

        mcast_recv::message_buffer *read_ir() const noexcept
        {
            auto m = new mcast_recv::message_buffer();
            auto nrec = chutil::mcast::receive(sock_ir, &m->message[0], mcast_recv::msgsz, addr_ir, addrlen_ir);
            if (nrec == 0)
            {
                delete m;
                m=nullptr;
                return 0;
            }
            m->len = nrec;
            auto seq_num = *(unsigned int *)(&m->message[0]);
            m->seqnum = seq_num;
            return m;
        }

        void start_handler(const actors::msg::Start *) noexcept
        {
        }

        void datarecovery_handler(const msg::DoDataRecovery *) noexcept
        {

            std::set<int32_t> recovered_books;

            log_inf("starting data recovery tim: %s", chutil::Time::now_utc().to_string());

            if (debug)
                std::cout << "DataRecoveryStart\n";

            chutil::mcast::create_udp_socket(port_dr, sock_dr, addr_dr, addrlen_dr);
            chutil::mcast::join_group(sock_dr, group_dr.c_str(), interface.c_str());

            uint curr_seq_num = 0;
            bool found_first_packet = false;
            uint32_t last_seq_num = std::numeric_limits<uint32_t>::max();
            uint32_t numreports = std::numeric_limits<uint32_t>::max();

            do
            {
            read_next_packet:
                auto msg = read_dr();
                ASSERT(msg, "could not read msg");
                auto databuf = &msg->message[0];
                auto data_start = databuf;

                const std::size_t sbe_message_header_size = 10;

                auto MsgSeqNum = *(unsigned int *)(databuf);

                //
                // have to wait for msg seq # 1
                //

                if (MsgSeqNum == 1)
                {
                    log_inf("found first packet");
                    if (debug)
                        std::cout << "found first packet\n";
                    found_first_packet = true;
                }

                if (!found_first_packet)
                {
                    delete msg;
                    msg=nullptr;
                    goto read_next_packet;
                }

                if (MsgSeqNum - curr_seq_num > 1 && MsgSeqNum != 1)
                {
                    if (debug)
                        std::cout << "**** packet gap "
                                  << " MsgSeqNum " << MsgSeqNum
                                  << " curr_seq_num " << curr_seq_num
                                  << " numreports " << numreports << std::endl;

                    // have packet gap
                    log_err("DataReceoveryRestart due to packet gap MsgSeqNum: %d, curr_seq_num: %d", MsgSeqNum, curr_seq_num);
                    delete msg;
                    msg=nullptr;
                    cb->DataReceoveryRestart();
                    curr_seq_num = 0;
                    recovered_books.clear();
                    found_first_packet = false;
                    numreports = std::numeric_limits<uint32_t>::max();
                    goto read_next_packet;
                }

                log_dbg("processing packet: %d, numreports: %d", MsgSeqNum, numreports);

                if (debug)
                    std::cout << "processing packet: " << MsgSeqNum << " numreports: " << numreports << std::endl;

                curr_seq_num = MsgSeqNum;

                databuf += sizeof(MsgSeqNum);

                auto SendingTime = *(unsigned long long *)(databuf);
                databuf += sizeof(SendingTime);

                while (std::size_t(databuf - data_start) < msg->len)
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

                    databuf -= sbe_message_header_size;

                    if (debug)
                    {
                        std::cout << "datarecovery_handler-----------------------------------------------------------------------------\n";
                        std::cout << "MsgSeqNum " << MsgSeqNum << " MsgSize: " << MsgSize << " TemplateID: " << TemplateID << " SchemaID: " << SchemaID << " Version: " << Version << "\n";
                        std::cout << "databuf - data_start " << (databuf - data_start) << " msg->len " << msg->len << std::endl;
                        std::cout << "datarecovery_handler-----------------------------------------------------------------------------\n";
                    }

                    if (TemplateID == 52)
                    {
#ifdef NOTIMPLEMENTED
                        sbe::SnapshotFullRefresh52 rec;
                        rec.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);
                        rec.noMDEntries();
                        auto txtim = rec.transactTime();
                        auto sec_id = rec.securityID();
                        auto mdentries = rec.noMDEntries();
                        while (mdentries.hasNext())
                        {
                            mdentries.next();
                            auto px_mant = mdentries.mDEntryPx().mantissa();
                            auto px_exp = mdentries.mDEntryPx().exponent();
                            auto sz = mdentries.mDEntrySize();
                            auto pxlevel = mdentries.mDPriceLevel();
                            auto numorders = mdentries.numberOfOrders();
                            auto side = mdentries.mDEntryType();
                            cb->SnapshotFullRefresh(
                                MsgSeqNum,
                                txtim,
                                SendingTime,
                                sec_id,
                                px_mant,
                                px_exp,
                                sz,
                                pxlevel,
                                numorders,
                                side);
                        }
#endif
                    }
                    else if (TemplateID == sbe::SnapshotFullRefreshOrderBook53::sbeTemplateId())
                    {
                        sbe::SnapshotFullRefreshOrderBook53 rec;
                        rec.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                        if (debug)
                        {
                            std::cout << rec << std::endl;
                        }

                        auto tmp_numreports = rec.totNumReports();
                        if (numreports != std::numeric_limits<uint32_t>::max())
                        {
                            if (numreports != tmp_numreports)
                            {
                                log_err("DataReceoveryRestart due to number of reports chnaged numreports: %d, tmp_numreports: %d",
                                        numreports, tmp_numreports);
                                if (debug)
                                {
                                    std::cout << "number of reports chnaged " << numreports << " to " << tmp_numreports << std::endl;
                                }
                                delete msg;
                                msg=nullptr;
                                cb->DataReceoveryRestart();
                                curr_seq_num = 0;
                                found_first_packet = false;
                                recovered_books.clear();
                                numreports = std::numeric_limits<uint32_t>::max();
                                goto read_next_packet;
                            }
                        }
                        else
                            numreports = tmp_numreports;
                        auto curr_chunk = rec.currentChunk();
                        auto chunks = rec.noChunks();
                        auto secid = rec.securityID();
                        auto local_last_seq_no = rec.lastMsgSeqNumProcessed();

                        if (debug)
                        {
                            std::cout
                                << "secid " << secid << " "
                                << " local_last_seq_no " << local_last_seq_no
                                << " last_seq_num " << last_seq_num
                                << std::endl;
                        }

                        last_seq_num = std::min(last_seq_num, local_last_seq_no);

                        if (chunks == curr_chunk)
                        {
                            log_dbg("recovereing book for secid: %d, chunks: %d, curr_chunk: %d",
                                    secid,
                                    chunks,
                                    curr_chunk);
                            if (debug)
                                std::cout << "recovered book " << secid << std::endl;
                            recovered_books.insert(secid);
                        }

                        if (debug)
                        {
                            std::cout << "secid " << secid << " found chunk " << curr_chunk << " numchunks " << chunks << std::endl;
                            std::cout << "curr_chunk: " << curr_chunk << " curr sec id " << secid
                                      << " last_seq_num " << last_seq_num
                                      << " local_last_seq_no " << local_last_seq_no
                                      << std::endl;
                        }

                        auto txtim = rec.transactTime();
                        auto mdentries = rec.noMDEntries();
                        while (mdentries.hasNext())
                        {
                            mdentries.next();
                            auto dispq = mdentries.mDDisplayQty();
                            auto px_mantissa = mdentries.mDEntryPx().mantissa();
                            auto px_exponent = mdentries.mDEntryPx().exponent();
                            auto mdEntryType = mdentries.mDEntryType();
                            auto order_prio = mdentries.mDOrderPriority();
                            auto order_id = mdentries.orderID();
                            cb->SnapshotFullRefreshOrderBook(
                                txtim,
                                SendingTime,
                                secid,
                                dispq,
                                px_mantissa,
                                px_exponent,
                                mdEntryType,
                                order_prio,
                                order_id);
                        }
                    }
                    else
                    {
                        std::cerr << TemplateID << std::endl;
                        ERR("got unknown TemplateID");
                    }

                    databuf += MsgSize;
                }

                delete msg;
                msg=nullptr;

                if (debug)
                {
                    std::cout << "reports recovered " << recovered_books.size()
                              << " expecting " << numreports - 1
                              << std::endl;
                }

            } while (recovered_books.size() < numreports);

            reply(new msg::EndDataRecovery(last_seq_num));
            close(sock_dr);

            log_inf("done data recovery tim: %s", chutil::Time::now_utc().to_string());
        }

        void instrumentrecovery_handler(const msg::DoInstrumentRecovery *) noexcept
        {

            std::set<int32_t> securities;
            log_inf("start instrument recovery tim: %s", chutil::Time::now_utc().to_string());

            if (debug)
                std::cout << "InstrumentRecoveryStart\n";

            chutil::mcast::create_udp_socket(port_ir, sock_ir, addr_ir, addrlen_ir);
            chutil::mcast::join_group(sock_ir, group_ir.c_str(), interface.c_str());

            uint32_t totnumreports;
            std::set<uint32_t> seq_num_processed;

            restart_ir_recovery:

            log_inf("starting instrument recovery");

            seq_num_processed.clear();
            totnumreports = std::numeric_limits<u_int32_t>::max();
            bool have_totnumreports = false;
            bool found_first_packet = false;

            do
            {
            read_next_packet_ir:

                auto set_tot_num_reports = [this,
                                            &have_totnumreports,
                                            &totnumreports //,
                                            //&found_first_packet,
                                            //&seq_num_processed,
                                            //&securities
                                        ](uint32_t new_tot_reports)
                {
                    if (!have_totnumreports)
                    {
                        have_totnumreports = true;
                        totnumreports = new_tot_reports;
                    }
                    else
                    {
                        if (new_tot_reports != totnumreports)
                        {
                            log_err("tot num reports changed from: %d, to: %d",
                                    totnumreports, new_tot_reports);

                            // totnumreports = std::numeric_limits<u_int32_t>::max();
                            // have_totnumreports = false;
                            // found_first_packet = false;
                            // securities.clear();
                            // return false;
                        }
                    }
                    return true;
                };

                auto msg = read_ir();
                ASSERT(msg, "no message");
                auto databuf = &msg->message[0];
                auto data_start = databuf;

                const std::size_t sbe_message_header_size = 10;

                auto MsgSeqNum = *(unsigned int *)(databuf);
                databuf += sizeof(MsgSeqNum);

                log_dbg("MsgSeqNum: %d", MsgSeqNum);

                if (MsgSeqNum == 1)
                {
                    log_inf("found first packet");
                    if (debug)
                        std::cout << "found first packet\n";

                    if (found_first_packet)
                    {
                        log_err("found first packet again");
                        goto restart_ir_recovery;
                    }
                    found_first_packet = true;
                }

                if (!found_first_packet)
                {
                    delete msg;
                    msg=nullptr;
                    goto read_next_packet_ir;
                }

                auto SendingTime = *(unsigned long long *)(databuf);
                databuf += sizeof(SendingTime);

                auto p = seq_num_processed.find(MsgSeqNum);
                if (p != seq_num_processed.end())
                {
                    delete msg;
                    msg=nullptr;
                    continue;
                }

                seq_num_processed.insert(MsgSeqNum);

                log_dbg("processing packet seq num: %d, processed so far: %d",
                        MsgSeqNum, seq_num_processed.size());

                while (std::size_t(databuf - data_start) < msg->len)
                {

                    auto MsgSize = *(unsigned short *)(databuf);
                    databuf += sizeof(MsgSize);

                    if (MsgSize == 0)
                    {
                        std::cerr << "instrumentrecovery_handler got message size 0\n";
                        SNGH;
                    }

                    auto BlockLength = *(unsigned short *)(databuf);
                    databuf += sizeof(BlockLength);

                    auto TemplateID = *(unsigned short *)(databuf);
                    databuf += sizeof(TemplateID);

                    auto SchemaID = *(unsigned short *)(databuf);
                    databuf += sizeof(SchemaID);

                    auto Version = *(unsigned short *)(databuf);
                    databuf += sizeof(Version);

                    databuf -= sbe_message_header_size;

                    if (debug)
                    {
                        std::cout << "instrumentrecovery_handler-----------------------------------------------------------------------------\n";
                        std::cout << "MsgSize: " << MsgSize << " TemplateID: " << TemplateID << " SchemaID: " << SchemaID << " Version: " << Version << "\n";
                        std::cout << "processed: " << (databuf - data_start) << " len: " << msg->len << std::endl;
                        std::cout << "num sec so far " << securities.size() << " excpected: " << totnumreports << std::endl;
                        std::cout << "instrumentrecovery_handler-----------------------------------------------------------------------------\n";
                    }

                    if (TemplateID == sbe::MDInstrumentDefinitionFuture54::sbeTemplateId())
                    {
                        sbe::MDInstrumentDefinitionFuture54 def;
                        def.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                        if (debug)
                        {
                            std::cout << def << std::endl;
                        }

                        set_tot_num_reports(def.totNumReports());

                        auto securityID = decode_MDInstrumentDefinitionFuture54(
                            0,
                            MsgSeqNum,
                            SendingTime,
                            def,
                            cb);

                        securities.insert(securityID);

                        log_dbg("got sec def for :%d", securityID);
                    }
                    else if (TemplateID == sbe::MDInstrumentDefinitionOption55::sbeTemplateId())
                    {
                        sbe::MDInstrumentDefinitionOption55 def;
                        def.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                        if (debug)
                        {
                            std::cout << def << std::endl;
                        }

                        set_tot_num_reports(def.totNumReports());

                        auto securityID = decode_MDInstrumentDefinitionOption55(
                            0,
                            MsgSeqNum,
                            SendingTime,
                            def,
                            cb);

                        securities.insert(securityID);

                        log_dbg("got sec def for :%d", securityID);
                    }
                    else if (TemplateID == sbe::MDInstrumentDefinitionSpread56::sbeTemplateId())
                    {
                        sbe::MDInstrumentDefinitionSpread56 def;

                        def.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                        if (debug)
                        {
                            std::cout << def << std::endl;
                        }

                        set_tot_num_reports(def.totNumReports());

                        auto securityID = decode_MDInstrumentDefinitionSpread56(
                            0,
                            MsgSeqNum,
                            SendingTime,
                            def,
                            cb);

                        securities.insert(securityID);

                        log_dbg("got sec def for :%d", securityID);
                    }
                    else if (TemplateID == sbe::MDInstrumentDefinitionFixedIncome57::sbeTemplateId())
                    {
                        sbe::MDInstrumentDefinitionFixedIncome57 def;
                        def.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                        if (debug)
                        {
                            std::cout << def << std::endl;
                        }

                        set_tot_num_reports(def.totNumReports());

                        auto securityID = decode_MDInstrumentDefinitionFixedIncome57(
                            0,
                            MsgSeqNum,
                            SendingTime,
                            def,
                            cb);

                        securities.insert(securityID);

                        log_dbg("got sec def for :%d", securityID);
                    }
                    else
                    {
                        std::cerr << "unkonwn template id " << TemplateID << " msgsize " << MsgSize
                                  << std::endl;
                        log_err("unkonwn template id %d msgsize %d", TemplateID, MsgSize);
                        //ERR("got unknown template id");
                    }

                    databuf += MsgSize;
                }

                delete msg;
                msg=nullptr;

                log_dbg("securities processed: %d, out of totnumreports: %d",
                        securities.size(), totnumreports);

            } while (securities.size() < totnumreports);

            close(sock_ir);

            reply(new msg::EndInstrumentRecovery());

            log_inf("done instrument recovery tim: %s", chutil::Time::now_utc().to_string());
        }
    };

}