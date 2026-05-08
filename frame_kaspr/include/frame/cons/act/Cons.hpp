#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <map>
#include <string>
#include <cstring>
#include <boost/algorithm/string/trim.hpp>
#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/act/Manager.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "chutil/CSVFileReader.hpp"
#include "logger/act/Logger.hpp"

// msg
#include "frame/cons/msg/Cmd.hpp"
#include "frame/cons/msg/CmdR.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"

namespace frame::cons::act
{

  class Cons : public actors::Actor
  {
    actors::Manager *mgr;
    std::map<std::string, std::string> password;
    char name[256];
    const char* get_name() const override { return name; }

  public:
    Cons(actors::Manager *_mgr, const std::string &_name) : mgr(_mgr) {
      strncpy(name, _name.c_str(), sizeof(name) - 1);
      name[sizeof(name) - 1] = '\0';

      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(frame::cons::msg::Cmd, cmd_handler);
    }

    void start_handler(const actors::msg::Start*) noexcept
    {
      log_inf("started");
    }

    void shutdown_handler(const actors::msg::Shutdown*) noexcept
    {
      log_inf("shutdown");
    }

    void cmd_handler(const frame::cons::msg::Cmd* m) noexcept
    {
      log_dbg("cmd: %s", m->buf);
      const auto &tks = chutil::CSVFileReader::tokenize(m->buf, ":");
      if (tks.size() < 1)
      {
        reply(new frame::cons::msg::CmdR("Error: did not get enough tokens: " + std::string(m->buf) + "\n"));
        return;
      }

      if (tks.size() == 1 && tks[0] == "list")
      {
        std::string rep;
        const auto &nams = mgr->get_managed_names();
        for (const auto &n : nams)
          rep += n + "\n";
        reply(new frame::cons::msg::CmdR(rep));
        return;
      }

      if (tks.size() % 2 != 0)
      {
        reply(new frame::cons::msg::CmdR("Error: num tokens is odd: " + std::string(m->buf) + "\n"));
        return;
      }

      try {
        auto actor = mgr->get_actor_by_name(tks[0]);
        if (!actor)
        {
          reply(new frame::cons::msg::CmdR("Error: actor not managed: " + std::string(m->buf) + "\n"));
          return;
        }

        std::map<std::string, std::string> kv;
        std::string what = tks.size() > 0 ? tks[1] : "";
        for (unsigned i = 2; i < tks.size(); i += 2)
          kv[tks[i]] = tks[i + 1];
        frame::cons::msg::Get get_msg(what, kv);
        auto ptr = actor.fast_send(&get_msg, this);
        if (!ptr.get())
        {
          reply(new frame::cons::msg::CmdR("Error: actor did not reply: " + std::string(m->buf) + "\n"));
          return;
        }

        auto page = dynamic_cast<const frame::cons::msg::Page *>(ptr.get());
        reply(new frame::cons::msg::CmdR(page->val));
        
      } catch (const std::exception& e) {
        log_err("Exception in cmd_handler: %s (cmd: %s)", e.what(), m->buf);
        reply(new frame::cons::msg::CmdR("Error: " + std::string(e.what()) + "\n"));
      }
    }
  };
}
