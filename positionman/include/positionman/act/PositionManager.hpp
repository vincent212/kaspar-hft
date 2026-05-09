#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "logger/act/Logger.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "light/qcoord.hpp"
#include "../msg/AddToPos.hpp"
#include "../msg/GetPos.hpp"
#include "../msg/Pos.hpp"
#include "enum/exch_code.hpp"
#include <sstream>
#include <map>
#include <vector>
#include <string>

namespace positionman
{
  struct PositionManager : public actors::Actor
  {
    const char* get_name() const override { return name; }

    char name[256];
    std::map<std::string, light::PCoord*> pcoords;  // instrument -> pcoord
    std::map<std::string, int> positions;            // instrument -> position

    PositionManager(const std::string& prefix, const std::map<std::string, light::PCoord*>& pcoords)
        : pcoords(pcoords)
    {
      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(frame::cons::msg::Get, get_handler);
      MESSAGE_HANDLER(msg::AddToPos, addtopos_handler);
      MESSAGE_HANDLER(msg::GetPos, get_pos_handler);
      snprintf(name, sizeof(name), "PosMgr_%s", prefix.c_str());
    }

    // Core function: add position for instrument
    int add_to_pos(const std::string& instrument, en::bs side, int sz)
    {
      ASSERT(sz > 0, "Size must be positive");
      if (side == en::bs::BUY)
        positions[instrument] += sz;
      else
        positions[instrument] -= sz;
      auto it = pcoords.find(instrument);
      if (it != pcoords.end() && it->second)
        it->second->add_position(side, sz);
      log_inf("add_to_pos inst=%s side=%s sz=%d pos=%d",
              instrument.c_str(), en::to_string(side), sz, positions[instrument]);
      return positions[instrument];
    }

    int get_pos(const std::string& instrument) const noexcept
    {
      auto it = positions.find(instrument);
      return it != positions.end() ? it->second : 0;
    }

    // Core handler: receive position update requests from Python/ZMQ
    void addtopos_handler(const msg::AddToPos* m)
    {
      log_inf("addtopos: inst=%s side=%s sz=%f", m->instrument.c_str(), en::to_string(m->side), m->sz);
      int sz = static_cast<int>(m->sz + 0.5);
      add_to_pos(m->instrument, m->side, sz);
    }

    void get_pos_handler(const msg::GetPos* m)
    {
      int pos = get_pos(m->instrument);
      reply(new msg::Pos(m->instrument, pos));
    }

    void start_handler(const actors::msg::Start*) { log_inf("started"); }
    void shutdown_handler(const actors::msg::Shutdown*) { log_inf("shutdown"); }

    void get_handler(const frame::cons::msg::Get* m)
    {
      if (m->what == "status" || m->what == "pos")
      {
        std::ostringstream oss;
        for (const auto& [inst, pos] : positions)
          oss << inst << ": " << pos << "\n";
        if (positions.empty())
          oss << "(no positions)\n";
        reply(new frame::cons::msg::Page(oss.str()));
      }
      else if (m->what == "add")
      {
        try
        {
          // MQ0 format: PosMgr_kaspr:add:sz:SIZE:sym:INSTRUMENT
          // Matches Python OrderManager:add_pos format (signed size, no side param)
          // Positive sz = BUY, Negative sz = SEL
          // m->kv already contains parsed key-value pairs

          // Check required parameters
          if (m->kv.find("sz") == m->kv.end() || m->kv.find("sym") == m->kv.end())
          {
            log_err("add command missing sz or sym parameter");
            reply(new frame::cons::msg::Page("ERROR: sz and sym parameters required (e.g., add:sz:10:sym:ESH6 or add:sz:-10:sym:ESH6)\n"));
            return;
          }

          std::string instrument = m->kv.at("sym");
          double signed_sz = std::stod(m->kv.at("sz"));

          if (signed_sz == 0)
          {
            log_err("add command: zero sz for sym=%s", instrument.c_str());
            reply(new frame::cons::msg::Page("ERROR: sz cannot be 0\n"));
            return;
          }

          // Determine side from sign: positive=BUY, negative=SEL
          std::string side_str = (signed_sz > 0) ? "BUY" : "SEL";
          if (!en::bs_is_valid(side_str))
          {
            log_err("add command: invalid side=%s for sym=%s", side_str.c_str(), instrument.c_str());
            reply(new frame::cons::msg::Page("ERROR: invalid side\n"));
            return;
          }
          en::bs side = en::bs_index_of(side_str);
          double sz = std::abs(signed_sz);

          log_inf("add command: inst=%s signed_sz=%f -> side=%s sz=%f",
                  instrument.c_str(), signed_sz, side_str, sz);

          // Send AddToPos message to self
          send(new msg::AddToPos(instrument, side, sz));

          std::ostringstream oss;
          oss << "✓ Sent AddToPos: " << instrument << " " << side_str << " " << sz << "\n";
          reply(new frame::cons::msg::Page(oss.str()));
        }
        catch (const std::exception& e)
        {
          log_err("Error handling add command: %s", e.what());
          std::ostringstream oss;
          oss << "ERROR: " << e.what() << "\n";
          reply(new frame::cons::msg::Page(oss.str()));
        }
      }
      else if (m->what == "close")
      {
        try
        {
          // MQ0 format: PosMgr_kaspr:close:sym:INSTRUMENT
          // Closes entire position (flattens to 0)

          // Check required parameters
          if (m->kv.find("sym") == m->kv.end())
          {
            log_err("close command missing sym parameter");
            reply(new frame::cons::msg::Page("ERROR: sym parameter required (e.g., close:sym:ESH6)\n"));
            return;
          }

          std::string instrument = m->kv.at("sym");

          // Get current position
          int current_pos = get_pos(instrument);
          if (current_pos == 0)
          {
            log_inf("close command: no position for %s", instrument.c_str());
            reply(new frame::cons::msg::Page("✓ No position to close\n"));
            return;
          }

          // Determine opposite side to flatten
          en::bs side = (current_pos > 0) ? en::bs::SEL : en::bs::BUY;
          double sz = std::abs(current_pos);
          std::string side_str = (current_pos > 0) ? "SEL" : "BUY";

          log_inf("close command: inst=%s current_pos=%d -> side=%s sz=%f",
                  instrument.c_str(), current_pos, side_str, sz);

          // Send AddToPos to flatten position
          send(new msg::AddToPos(instrument, side, sz));

          std::ostringstream oss;
          oss << "✓ Closing position: " << instrument << " " << side_str << " " << sz
              << " (was " << current_pos << ")\n";
          reply(new frame::cons::msg::Page(oss.str()));
        }
        catch (const std::exception& e)
        {
          log_err("Error handling close command: %s", e.what());
          std::ostringstream oss;
          oss << "ERROR: " << e.what() << "\n";
          reply(new frame::cons::msg::Page(oss.str()));
        }
      }
      else
      {
        std::ostringstream oss;
        oss << "ERROR: Unknown command '" << m->what << "'\n";
        oss << "Available commands: status, pos, add:sz:SIZE:sym:INSTRUMENT:side:SIDE\n";
        reply(new frame::cons::msg::Page(oss.str()));
      }
    }
  };
}
