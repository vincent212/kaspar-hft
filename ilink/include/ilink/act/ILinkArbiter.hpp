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
#include "ilink/msg/DoInitialize.hpp"
#include "ilink/msg/DoBind.hpp"
#include "ilink/msg/Connected.hpp"
#include "ilink/msg/Disconnected.hpp"
#include "ilink/msg/InitPrimary.hpp"
#include "ilink/msg/InitSecondary.hpp"
#include "frame/som/msg/SetExchManager.hpp"
#include "logger/act/Logger.hpp"
#include "actors/msg/Subscribe.hpp"
#include "enum/e_names.hpp"

namespace ilink
{

    class ILinkArbiter : public actors::Actor
    {

    private:
        const char* get_name() const { return name; }
        char name[256];
        actor_ptr handler_primary, handler_secondary;
        std::vector<actor_ptr> subs;
        en::x exch;
        int disconnect_count = 0;

    public:
        ILinkArbiter(
            en::x _exch, 
            [[maybe_unused]] bool _doinit, 
            actor_ptr _handler_primary, 
            actor_ptr _handler_secondary)
            : //do_init(_doinit),
              handler_primary(_handler_primary), 
              handler_secondary(_handler_secondary),
              exch(_exch)
        {
            // Initialize name once in constructor
            snprintf(name, sizeof(name), "ILinkArbiter%s", en::to_string(exch));

            MESSAGE_HANDLER(actors::msg::Start, start_handler);
            MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
            MESSAGE_HANDLER(ilink::msg::Connected, connected_handler); // sent from iLinkHandler
            MESSAGE_HANDLER(ilink::msg::Disconnected, disconnected_handler); // sent from iLinkHandler
            MESSAGE_HANDLER(ilink::msg::InitPrimary, init_primary_handler); // sent from iLinkHandler
            MESSAGE_HANDLER(ilink::msg::InitSecondary, init_secondary_handler); // sent from iLinkHandler
            ASSERT(handler_primary, "no primary handler");
            ASSERT(handler_secondary, "no secondary handler");
        }

        void subscribe(actor_ptr _sub)
        {
            subs.push_back(_sub);
        }

    private:

        void init_primary_handler(const ilink::msg::InitPrimary *)
        {
            log_inf("init_primary_handler");
            handler_primary->send(new ilink::msg::DoInitialize(), this);
        }

        void init_secondary_handler(const ilink::msg::InitSecondary *)
        {
            log_inf("init_secondary_handler");
            handler_secondary->send(new ilink::msg::DoInitialize(), this);
        }

        // subscribe handler
        void subscribe_handler(const actors::msg::Subscribe *m)
        {
            ASSERT(m->sender, "no sender for subscribe");
            subs.push_back(m->sender);
        }

        // start handler
        void start_handler(const actors::msg::Start *)
        {
            log_inf("start_handler");
        }

        // shutdown handler
        void shutdown_handler(const actors::msg::Shutdown *)
        {
        }

        // connected handler
        void connected_handler(const ilink::msg::Connected *msg)
        {
            if (msg->is_primary)
            {
                log_inf("primary is connected");
                log_inf("setting primary as default handler");
                ASSERT(handler_primary, "no primary handler");
                for (auto &s : subs)
                    s->send(new frame::som::msg::SetExchManager(exch, handler_primary), this);
            }
            else
            {
                log_inf("secondary is connected setting secondary as default handler");
                ASSERT(handler_secondary, "no secondary handler");
                for (auto &s : subs)
                    s->send(new frame::som::msg::SetExchManager(exch, handler_secondary), this);
            }
        }

        // disconnected handler
        void disconnected_handler(const ilink::msg::Disconnected *msg)
        {
            disconnect_count++;
            log_inf("disconnected_handler: disconnect_count: %d", disconnect_count);
            if (disconnect_count > 2)
            {
                log_err("disconnect_count > 2, terminating");
                ERR("disconnect_count > 2, terminating");
            }
            if (msg->is_primary)
            {
                log_inf("primary is disconnected");
                for (auto &s : subs)
                    s->send(new frame::som::msg::SetExchManager(exch, 0), this);
            }
            else
            {
                log_inf("secondary is disconnected");
                for (auto &s : subs)
                    s->send(new frame::som::msg::SetExchManager(exch, 0), this);
            }
        }
    };
}