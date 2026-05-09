#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "logger/act/Logger.hpp"
#include "frame/ref/RefData.hpp"

#include "frame/ob/msg/BBBOChg.hpp"
#include "frame/ob/msg/BBBOSub.hpp"
#include "frame/ob/msg/TradeNotify.hpp"

#include "frame/som/msg/GetPosition.hpp"
#include "frame/som/msg/PositionResponse.hpp"
#include "frame/som/msg/UpdatePosition.hpp"
#include "frame/som/msg/ResetPositions.hpp"
#include "frame/som/msg/FillSub.hpp"
#include "frame/som/msg/Fill.hpp"

#include <map>

namespace posttrade::act
{

  struct DB : public actors::Actor
  {
    en::x mmVenue;
    std::vector<std::vector<actor_ptr>> order_books;
    actor_ptr som_actor;

    const char* get_name() const override { return "DB"; }

    DB(
        en::x mmVenue = en::x::UNI,
        const std::vector<std::vector<actor_ptr>> &order_books = {},
        actor_ptr som = nullptr)
        : mmVenue(mmVenue),
          order_books(order_books),
          som_actor(som)
    {
      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(frame::cons::msg::Get, get_handler);
      MESSAGE_HANDLER(frame::ob::msg::BBBOChg, bbbochg_handler);
      MESSAGE_HANDLER(frame::ob::msg::TradeNotify, trade_notify_handler);
      MESSAGE_HANDLER(frame::som::msg::GetPosition, get_position_handler);
      MESSAGE_HANDLER(frame::som::msg::UpdatePosition, update_position_handler);
      MESSAGE_HANDLER(frame::som::msg::ResetPositions, reset_positions_handler);
      MESSAGE_HANDLER(frame::som::msg::Fill, fill_handler);
    }

    void start_handler(const actors::msg::Start *)
    {
      log_inf("DB starting");

      if (!order_books.empty())
      {
        const std::size_t n = frame::ref::RefData::inst().num_assets();
        for (const auto &ob : order_books)
          for (std::size_t i = 0; i < n; i++)
            if (ob[i])
              ob[i]->send(new frame::ob::msg::BBBOSub(), this);
      }

      if (som_actor)
      {
        som_actor->send(new frame::som::msg::FillSub(), this);
        log_inf("Subscribed to SOM fill notifications");
      }
    }

    void shutdown_handler(const actors::msg::Shutdown *) {}
    void get_handler(const frame::cons::msg::Get *) {}

    void trade_notify_handler(const frame::ob::msg::TradeNotify *) {}

    void bbbochg_handler(const frame::ob::msg::BBBOChg *) {}

    void get_position_handler(const frame::som::msg::GetPosition *) {}
    void update_position_handler(const frame::som::msg::UpdatePosition *) {}
    void reset_positions_handler(const frame::som::msg::ResetPositions *) {}

    void fill_handler(const frame::som::msg::Fill *) {}
  };
}
