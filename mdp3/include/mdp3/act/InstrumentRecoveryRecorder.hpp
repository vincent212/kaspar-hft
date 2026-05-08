
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

#include "mdp3/msg/DoInstrumentRecovery.hpp"
#include "mdp3/msg/EndInstrumentRecovery.hpp"

#include "actors/act/Timer.hpp"
#include "actors/msg/Timeout.hpp"

namespace mdp3
{

    class InstrumentRecoveryRecorder : public actors::Actor
    {

        const char* get_name() const { return "InstrumentRecoveryRec"; }

    public:
        InstrumentRecoveryRecorder(
            feed_handler_if *_cb,
            in_port_t _port_ir,
            const char *_group_ir,
            const char *_interface) : cb(_cb),
                                      port_ir(_port_ir),
                                      group_ir(_group_ir),
                                      interface(_interface)
        {
            MESSAGE_HANDLER(actors::msg::Start, start_handler);
            MESSAGE_HANDLER(msg::DoInstrumentRecovery, instrumentrecovery_handler);
            MESSAGE_HANDLER(actors::msg::Timeout, timeout_handler);
        }

    private:
        in_port_t port_ir;
        int sock_ir;
        struct ip_mreq ip_mreq;
        struct sockaddr_in addr_ir;
        std::size_t addrlen_ir;
        std::string group_ir, interface;
        feed_handler_if *cb;
        int64_t last_ts;

        mcast_recv:: message_buffer *read_ir() const noexcept
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
            send(new msg::DoInstrumentRecovery(), this);
            actors::act::Timer::wake_up_in(this, 60);
        }

        void timeout_handler(const actors::msg::Timeout *) noexcept
        {
            auto currtim = chutil::Time::epoch();
            if (last_ts)
            {
                auto interval = (double(currtim) - last_ts) / 1e9;
                if (interval > 31)
                {
                    log_err("last message was more than 30s %f", interval);
                    chutil::mcast::drop_group(sock_ir, &ip_mreq);
                    close(sock_ir);
                    instrumentrecovery_handler(0);
                }
            }
            actors::act::Timer::wake_up_in(this, 31);
        }

        void instrumentrecovery_handler(const msg::DoInstrumentRecovery *) noexcept
        {

            log_inf("start instrument recovery recorder tim: %s, port: %d", chutil::Time::now_utc().to_string(), port_ir);

            chutil::mcast::create_udp_socket(port_ir, sock_ir, addr_ir, addrlen_ir);
            ip_mreq = chutil::mcast::join_group(sock_ir, group_ir.c_str(), interface.c_str());

            log_inf("joined multicast group %s on %s", group_ir, interface);

            do
            {
                auto msg = read_ir();
                if (!msg)
                {
                    log_err("could not read message restarting");
                    chutil::mcast::drop_group(sock_ir, &ip_mreq);
                    close(sock_ir);
                    send(new msg::DoInstrumentRecovery(), this);
                    return;
                }
                last_ts = chutil::Time::epoch();
                //log_inf("%d", last_ts);
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

                    //log_inf("TemplateID: %d", TemplateID);


                    if (TemplateID == 54)
                    {
                        sbe::MDInstrumentDefinitionFuture54 def;
                        def.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                        decode_MDInstrumentDefinitionFuture54(
                            0,
                            MsgSeqNum,
                            SendingTime,
                            def,
                            cb);

                    }
                    else if (TemplateID == 55)
                    {
                        sbe::MDInstrumentDefinitionOption55 def;
                        def.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                        decode_MDInstrumentDefinitionOption55(
                            0,
                            MsgSeqNum,
                            SendingTime,
                            def,
                            cb);

                    }
                    else if (TemplateID == 56)
                    {
                        sbe::MDInstrumentDefinitionSpread56 def;

                        def.wrapForDecode(databuf, sbe_message_header_size, BlockLength, Version, MsgSize);

                        decode_MDInstrumentDefinitionSpread56(
                            0,
                            MsgSeqNum,
                            SendingTime,
                            def,
                            cb);

                    }
                    else
                    {
                        std::cerr << "unkonwn template id " << TemplateID << " msgsize " << MsgSize
                                  << std::endl;
                        log_err("unkonwn template id %d msgsize %d", TemplateID, MsgSize);
                        //ERRF(boost::format("Instrument recovery unkonwn template id %d msgsize %d") % TemplateID % MsgSize);
                    }

                    databuf += MsgSize;
                }

                delete msg;

            } while (true);

            log_inf("end instrument recovery rec tim: %s, port: %d", chutil::Time::now_utc().to_string(), port_ir);

            chutil::mcast::drop_group(sock_ir, &ip_mreq);
            close(sock_ir);
        }
    };

}