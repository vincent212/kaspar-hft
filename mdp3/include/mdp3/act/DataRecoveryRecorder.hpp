
#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

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
#include "mdp3/msg/EndDataRecovery.hpp"

#include "actors/act/Timer.hpp"
#include "actors/msg/Timeout.hpp"

namespace mdp3
{

    class DataRecoveryRecorder : public actors::Actor
    {

        const char* get_name() const { return "DataRecoveryRecorder"; }

    public:
        DataRecoveryRecorder(
            feed_handler_if *_cb,
            in_port_t _port_dr,
            const char *_group_dr,
            const char *_interface)
            : cb(_cb),
              port_dr(_port_dr),
              group_dr(_group_dr),
              interface(_interface)
        {
            MESSAGE_HANDLER(actors::msg::Start, start_handler);
            MESSAGE_HANDLER(msg::DoDataRecovery, datarecovery_handler);
            MESSAGE_HANDLER(actors::msg::Timeout, timeout_handler);
        }

    private:
        in_port_t port_dr;
        int sock_dr;
        struct sockaddr_in addr_dr;
        std::size_t addrlen_dr;
        std::string group_dr, interface;
        feed_handler_if *cb;
        uint64_t last_ts;

        
        mcast_recv:: message_buffer *read_dr() const noexcept
        {
            auto m = new mcast_recv:: message_buffer();
            auto nrec = chutil::mcast::receive(sock_dr, &m->message[0], mcast_recv ::msgsz, addr_dr, addrlen_dr);
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
            send(new msg::DoDataRecovery(), 0);
            actors::act::Timer::wake_up_in(this, 60);

        }

        void timeout_handler(const actors::msg::Timeout *) noexcept
        {
            if (last_ts)
            {
                auto currtim = chutil::Time::epoch();
                auto interval = (double(currtim) - last_ts) / 1e9;
                if (interval > 31)
                {
                    log_err("last message was more than 30s %f", interval);
                    close(sock_dr);
                    datarecovery_handler(0);
                }
            }
            actors::act::Timer::wake_up_in(this, 31);
        }

        void datarecovery_handler(const msg::DoDataRecovery *) noexcept
        {
            std::set<int32_t> recovered_books;

            log_inf("starting data recovery channel recording tim: %s, port: %d", chutil::Time::now_utc().to_string(), port_dr);

            chutil::mcast::create_udp_socket(port_dr, sock_dr, addr_dr, addrlen_dr);
            auto ip_mreq = chutil::mcast::join_group(sock_dr, group_dr.c_str(), interface.c_str());

            log_inf("joined multicast group %s on %s", group_dr, interface);

            do
            {
                auto msg = read_dr();
                if (!msg)
                {
                    log_err("could not read message from socket, restarting");
                    send(new msg::DoDataRecovery(), 0);
                    return;
                }
                last_ts = chutil::Time::epoch();
                //log_inf("%d",last_ts);

                ASSERT(msg, "could not read msg");
                auto databuf = &msg->message[0];
                auto data_start = databuf;

                const std::size_t sbe_message_header_size = 10;

                auto MsgSeqNum = *(unsigned int *)(databuf);

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

                    //log_inf("TemplateID: %d", TemplateID);

                    if (TemplateID == 52)
                    {
                        // not implemented
                    }
                    else if (TemplateID == 53)
                    {
                        sbe::SnapshotFullRefreshOrderBook53 rec;
                        rec.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                        auto numreports = rec.totNumReports();
                        auto curr_chunk = rec.currentChunk();
                        auto chunks = rec.noChunks();
                        auto secid = rec.securityID();
                        auto last_seq_num = rec.lastMsgSeqNumProcessed();

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

                            cb->SnapshotFullRefreshOrderBook_NR(
                                last_seq_num,
                                numreports,
                                MsgSeqNum,
                                txtim,
                                SendingTime,
                                curr_chunk,
                                chunks,
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

            } while (true);

            chutil::mcast::drop_group(sock_dr, &ip_mreq);
            close(sock_dr);

            log_inf("done data recovery tim: %s", chutil::Time::now_utc().to_string());
        }
    };

}