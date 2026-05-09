#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Continue.hpp"

#include "ilink/ILinkRcv.hpp"
#include "ilink/ILinkCBImp.hpp"

#include "ilink/msg/DoTerminate.hpp"
#include "ilink/msg/StartReceiving.hpp"

#include "logger/act/Logger.hpp"

#include "enum/e_names.hpp"

namespace ilink
{

    class ILinkReceiver : public actors::Actor
    {

        const bool debug = false;
        const bool block = true;
        bool terminated = true;
        int nodatacnt = 0;

        en::x exch;
        char name[256];

    private:
        const char* get_name() const
            { return name; }
        int sock;
        m2::ilink::CBIF *impl;

        char buffer[265000];

    public:
        ILinkReceiver(
            en::x _exch,
            int _sock, 
            m2::ilink::CBIF *_impl)
            : impl(_impl), sock(_sock), exch(_exch)
        {
            snprintf(name, sizeof(name), "IlinkReceiver_%d_%s", _sock, en::to_string(_exch));
            MESSAGE_HANDLER(actors::msg::Start, start_handler);
            MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
            MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
            MESSAGE_HANDLER(ilink::msg::DoTerminate, terminate_handler);
            MESSAGE_HANDLER(ilink::msg::StartReceiving, startreceiving_handler);
        }

    private:
        // startreceiving handler
        void startreceiving_handler(const ilink::msg::StartReceiving *)
        {
            log_inf("startreceiving_handler");
            terminated = false;
        }

        // terminate handler
        void terminate_handler(const ilink::msg::DoTerminate *)
        {
            log_inf("terminate_handler");
            terminated = true;
        }

        // start handler
        void start_handler(const actors::msg::Start *)
        {
            send(new actors::msg::Continue(), 0);
        }

        // shutdown handler
        void shutdown_handler(const actors::msg::Shutdown *)
        {
        }

        // continue handler
        void continue_handler(const actors::msg::Continue *)
        {
            if (terminated)
            {
                sleep(1);
                send(new actors::msg::Continue(), 0);
                return;
            }
            memset(buffer, 0, sizeof(buffer));
            if (m2::ilink::receiver::process_message_from_msgw(sock, buffer, impl, block, debug))
                send(new actors::msg::Continue(), 0);
            else
            {
                log_err("continue_handler: process_message_from_msgw returned false");
                sleep(1);
                if (nodatacnt++ < 3)
                    send(new actors::msg::Continue(), 0);
                else
                {
                    log_err("continue_handler: no data for 3 attempts");
                }
            }
        }
    };
}