#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include "chutil/Macros.hpp"
#include "chutil/Table.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Continue.hpp"
#include "chutil/udp_socket.hpp"
#include <map>
//#include <flat_map>
#include <chrono>
#include "mcast_recv/message_buffer.hpp"
#include "mdp3/DataDecoder.hpp"
#include "mdp3/msg/DoDataRecovery.hpp"
#include "mdp3/msg/DoInstrumentRecovery.hpp"
#include "mdp3/msg/EndDataRecovery.hpp"
#include "mdp3/msg/EndInstrumentRecovery.hpp"
#include "mcast_recv/msg/ProcessQ.hpp"
#include "mdp3/msg/StopQ.hpp"
#include "mdp3/msg/StartQ.hpp"
#include "mdp3/mbo_if.hpp"
#include "logger/act/Logger.hpp"
#include "actors/act/Timer.hpp"
#include "actors/msg/Timeout.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"

namespace mdp3
{

    class MessageProcessor : public actors::Actor
    {

    private:
        const char* get_name() const { return cached_name; }
        std::string chan_nam;
        char cached_name[256];

        actors::Actor *recovery_processor;
        DataDecoder decoder;
        //boost::container::flat_map<uint32_t, mcast_recv::message_buffer> msg_q;
        std::map<uint32_t, mcast_recv::message_buffer> msg_q;
        bool dorecovery = false;
        bool in_data_recovery = false;
        bool in_instr_recovery = false;
        //bool have_instruments = false;
        bool instr_recoveryonstart = true;
        bool data_recoveryonstart = false;
        //bool have_seq_num = false;
        uint32_t qseq_num = 0;
        std::vector<double> read_cnt;
        uint32_t numdrecoveries = 0;
        uint32_t numwaits = 0;
        int waitcnt = 3;
        const int maxwaitcnt = 3;
        uint64_t last_ts = 0;
        uint64_t last_msg_timestamp = 0;  // Last message timestamp (not recv_ts)
        bool stopq = false;
        uint32_t nummsg = 0;

    public:
        MessageProcessor(
            const std::string _chan_nam,
            actors::Actor *_recovery_processor,
            mdp3::feed_handler_if *_cb,
            bool _dorecovery,
            bool _recoveryonstart,
            bool _disable_mbo,
            uint32_t _max_mbp_level,
            bool _debug = false)
            : decoder(_cb, _disable_mbo, _max_mbp_level, _debug),
              recovery_processor(_recovery_processor),
              dorecovery(_dorecovery),
              data_recoveryonstart(_recoveryonstart),
              chan_nam(_chan_nam)
        {
            snprintf(cached_name, sizeof(cached_name), "%sMessageProcessor", _chan_nam.c_str());
            MESSAGE_HANDLER(actors::msg::Start, start_handler);
            MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
            MESSAGE_HANDLER(mcast_recv::msg::ProcessQ<uint32_t>, processq_handler);
            MESSAGE_HANDLER(msg::EndDataRecovery, enddatarecovery_handler);
            MESSAGE_HANDLER(msg::EndInstrumentRecovery, endinstrrecovery_handler);
            MESSAGE_HANDLER(actors::msg::Timeout, timeout_handler);
            MESSAGE_HANDLER(msg::StartQ, startq_handler); // who sends this?
            MESSAGE_HANDLER(msg::StopQ, stopq_handler);   // who sends this?
            MESSAGE_HANDLER(frame::cons::msg::Get, get_handler);

            // if (dorecovery)
            // {
            //     data_recoveryonstart = true;
            // }
        }

    private:
        void get_handler(const frame::cons::msg::Get *m) noexcept
        {
            if (m->what == "stopq")
            {
                send(new mdp3::msg::StopQ(), this);
                reply(new frame::cons::msg::Page("stopping q"));
            }
            else if (m->what == "startq")
            {
                send(new mdp3::msg::StartQ(), this);
                reply(new frame::cons::msg::Page("starting q"));
            }
            else if (m->what == "stats")
            {
                std::vector<std::string> labels = {"metric", "value"};
                chutil::Table t("MDP3 MessageProcessor Statistics", labels);

                t.add_row();
                t.set_value("Messages Processed");
                t.set_value(nummsg);

                t.add_row();
                t.set_value("Last Message Timestamp");
                t.set_value(last_msg_timestamp);

                // Check time since last message
                uint64_t now = chutil::Time::epoch();
                double seconds_since_last_msg = double(now - last_msg_timestamp) / 1e9;

                t.add_row();
                t.set_value("Seconds Since Last Msg");
                t.set_value(seconds_since_last_msg);

                t.add_row();
                t.set_value("More than 10s ago");
                t.set_value(seconds_since_last_msg > 10.0);

                t.add_row();
                t.set_value("More than 30s ago");
                t.set_value(seconds_since_last_msg > 30.0);

                t.add_row();
                t.set_value("Recoveries");
                t.set_value(numdrecoveries);

                reply(new frame::cons::msg::Page(t.to_string()));
            }
            else
            {
                reply(new frame::cons::msg::Page("Unknown command. Use: stats, stopq, startq"));
            }
        }

        void startq_handler(const mdp3::msg::StartQ *) noexcept
        {
            log_inf("startq");
            stopq = false;
        }

        void stopq_handler(const mdp3::msg::StopQ *) noexcept
        {
            log_inf("stopq");
            stopq = true;
        }

        void timeout_handler(const actors::msg::Timeout *) noexcept
        {
            auto currtim = chutil::Time::epoch();
            if (last_ts) {
            auto interval = (double(currtim) - last_ts) / 1e9;
            if (interval > 30)
            {
                log_err("last message was more than 30s %f, nummsg: %d", interval, nummsg);
            }
            }
            actors::act::Timer::wake_up_in(this, 31);
        }

        void start_handler(const actors::msg::Start *) noexcept
        {
            actors::act::Timer::wake_up_in(this, 60);
            if (instr_recoveryonstart)
            {
                log_inf("starting with insrument recovery");
                do_instr_recovery();
            }
            else
            {
                log_inf("starting without insrument recovery");
            }
            if (data_recoveryonstart)
            {
                log_inf("initiating data recovery on start");
                do_data_recovery();
            }
            else
            {
                log_inf("starting without data recovery");
            }
        }

        void processq_handler(const mcast_recv::msg::ProcessQ<uint32_t> *m) noexcept
        {
            nummsg++;
            if (m->buf.seqnum <= qseq_num || stopq)
            {
                return;
            }
            // try_emplace, not operator[]: operator[] would value-initialize a
            // fresh ~2KB message_buffer node (zero-filling the 2000-byte array)
            // and then copy-assign over it. try_emplace copy-constructs the node
            // directly -- no zero-fill -- and does nothing on a duplicate seqnum
            // (a retransmit carries identical bytes, so first-wins == last-wins).
            msg_q.try_emplace(m->buf.seqnum, m->buf);
            processq(m->buf.recv_ts, m->last);
            last_ts = m->buf.recv_ts;
            // Update last_msg_timestamp if recv_ts is not 0
            if (m->buf.recv_ts != 0)
            {
                last_msg_timestamp = m->buf.recv_ts;
            }
        }

        void do_data_recovery()
        {
            log_inf("initiating data recovery");
            recovery_processor->send(new msg::DoDataRecovery(), this);
            in_data_recovery = true;
            numdrecoveries++;
            waitcnt = maxwaitcnt;
        }

        void do_instr_recovery()
        {
            if (recovery_processor)
            {
                log_inf("initiating instrument recovery");
                recovery_processor->send(new msg::DoInstrumentRecovery(), this);
                in_instr_recovery = true;
            }
            else
            {
                log_err("no recovery processor");
            }
        }

        void processq(uint64_t ts, [[maybe_unused]] bool last)
        {

            if CHUNLIKELY (in_data_recovery || in_instr_recovery)
            {
                return;
            }

#ifdef FORCEINSTRUMENTRECOVERY
            if (!have_instruments)
            {
                log_inf("do not have instruments but got data messaage initiating recovery");
                if (recovery_processor)
                {
                    log_inf("initiating instrument recovery");
                    recovery_processor->send(new msg::DoInstrumentRecovery(), this);
                    in_instr_recovery = true;
                }
                else
                {
                    log_err("no recovery processor");
                }
                have_instruments = true;
                return;
            }
#endif

            auto p = msg_q.begin();
            if (p == msg_q.end())
                return;

            while (p != msg_q.end())
            {

                auto sn = p->first; // key: used after the erase (qseq_num = sn)

                if (sn <= qseq_num)
                {
                    log_dbg("dropping message sn: %d, qseq_num: %d", sn, qseq_num);
                    p = msg_q.erase(p); // returns the next element (== begin here)
                }
                else if (sn == qseq_num + 1 || qseq_num == 0)
                {
                    // process message
                    log_dbg("processing message sn: %d, qseq_num: %d", sn, qseq_num);
                    bool is_channel_reset = false;
                    // Ingress mailbox depth for THIS packet, read from the
                    // reorder-map node (p->second) -- the packet actually being
                    // decoded, not whatever packet happened to trigger this
                    // drain. (Contrast `ts` below: the arriving packet's recv_ts,
                    // which is wrong for a packet released out of a gap.) Decoding
                    // straight from the node also avoids the ~1.5KB copy-out.
                    decoder.set_ingress_qlen(p->second.qlen);
                    auto rc = decoder.mbo_data(&p->second.message[0], p->second.len, ts, is_channel_reset);
                    if (!rc)
                    {
                        log_err("could not decode critical data message initiating recovery");
                        decoder.gap();
                        do_data_recovery();
                        return;
                    }

                    if (is_channel_reset)
                    {
                        log_err("****** channel reset ****** sn: %d, qseq_num: %d, is_channel_reset: %d", sn, qseq_num, is_channel_reset);
                    }

                    p = msg_q.erase(p); // returns the next element (== begin here)
                    qseq_num = sn;
                    if (waitcnt < maxwaitcnt)
                    {
                        log_inf("gap has closed waitcnt: %d", waitcnt);
                        waitcnt = maxwaitcnt;
                    }
                }
                else if (--waitcnt > 0)
                {
                    numwaits++;
                    // ERR, not WRN, and carries its own tim:. This line is the
                    // TRUE start of latency contamination -- packets begin
                    // buffering in msg_q here, before any recovery event fires.
                    // Without a call-site stamp it cannot be placed on the
                    // timeline at all, because the logger's line prefix is
                    // 00/00/0000 00:00:00.000000000 (Logger.cpp rt=false).
                    log_err("waiting for gap to close waitcnt: %d, numwaits: %d, tim: %s",
                            waitcnt, numwaits, chutil::Time::now_utc().to_string());
                    return;
                }
                else
                {
                    // we have a gap and its not start
                    log_err("have gap sn: %d, expected: %d, tim: %s", sn, qseq_num + 1,
                            chutil::Time::now_utc().to_string());

                    if (dorecovery)
                    {
                        log_wrn("recover is on clearing packet q and resetting sn");
//#define INSTRECOVERYONGAP
#ifdef INSTRECOVERYONGAP
                        log_inf("will do instr recovery");
                        do_instr_recovery(); 
#endif
                        log_inf("will do data recovery");
                        do_data_recovery();
                    }
                    else
                    {
                        // recovery is not on
                        log_wrn("no recovery resetting sn to %d", sn);
                        qseq_num = sn - 1;
                    }
                    break;
                }
            }
#ifdef SENDEOBURST
            if (last)
                decoder.burstend();
#endif
        }

        void enddatarecovery_handler(const msg::EndDataRecovery *m) noexcept
        {
            log_inf("data recovery done recovered seq num: %d, current: %d",
                    m->last_seq,
                    qseq_num);
            if (qseq_num < m->last_seq)
                qseq_num = m->last_seq;
            processq(0, 0);
            in_data_recovery = false;
        }

        void endinstrrecovery_handler(const msg::EndInstrumentRecovery *) noexcept
        {
            log_inf("instrument recovery done");
            processq(0, 0);
            in_instr_recovery = false;
        }

        void
        shutdown_handler(const actors::msg::Shutdown *) noexcept
        {
            log_inf("shutdown");

            decoder.print_stats();
        }
    };

}