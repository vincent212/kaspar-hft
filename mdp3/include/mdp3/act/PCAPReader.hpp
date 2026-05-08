#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#error "do not use this file -- not implemented -- use the one in mcast_recv/act/PCAPReader.hpp"

#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Continue.hpp"
#include "chutil/udp_socket.hpp"
#include <map>
#include <chrono>
#include "mcast_recv/message_buffer.hpp"
#include "mcast_recv/msg/ProcessQ.hpp"
#include "logger/act/Logger.hpp"
#include "oogsl/gvector.hpp"

#include "actors/act/Timer.hpp"
#include "actors/msg/Timeout.hpp"

namespace mdp3
{

    class PCAPReader : public actors::Actor
    {

    public:
        PCAPReader(const std::string &_fname
                   )
            : fname(_fname)
        {
            // open binary file fname for reading
            std::ifstream file(fname, std::ios::binary);
            if (!file.is_open())
            {
                std::cerr << "Could not open file " << fname << std::endl;
                SNGH;
            }
            MESSAGE_HANDLER(actors::msg::Start, start_handler);
            MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
        }

    private:
        std::string fname;
        //actors::Actor *msg_processor;
        //const char chan;

        mcast_recv:: message_buffer *read(uint32_t &seq_num) const noexcept
        {
            auto m = new mcast_recv::message_buffer();
            auto nrec = 0; // read from file
            if (nrec == 0)
            {
                delete m;
                return 0;
            }
            m->len = nrec;
            seq_num = *(unsigned int *)(&m->message[0]);
            m->seqnum = seq_num;
            return m;
        }

        void start_handler(const actors::msg::Start *) noexcept
        {
            send(new actors::msg::Continue(), 0);
        }

        void continue_handler(const actors::msg::Continue *) noexcept
        {

            mcast_recv::message_buffer *msg;
            uint32_t seq = 0;

            msg = read(seq);
            if (msg)
            {
                // auto rcvts = chutil::Time::epoch();
                // todo: need to pass fake params to ProcessQ 
                //msg_processor->send(new mcast_recv::msg::ProcessQ<uint32_t>(chan, rcvts, seq, msg), 0);
                SNGH;
                send(new actors::msg::Continue(), 0);
            }
            else
            {
            }
        };
    };

}