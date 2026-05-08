#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/act/Manager.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/msg/Timeout.hpp"
#include "chutil/Table.hpp"
#include "logger/act/Logger.hpp"

#include <boost/format.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "frame/cons/msg/CmdR.hpp"
#include "frame/cons/msg/Cmd.hpp"

#include <zmq.h>
#include <optional>
#include <string>

namespace mq0::act
{
  struct MQ0_server : public actors::Actor
  {

    const char* get_name() const override
    {
      return name_.c_str();
    }

    std::string name_;
    int port;
    void *context = 0, *responder = 0;
    actor_ptr console;

  public:
    MQ0_server(
        const std::string &name,
        int port,
        actor_ptr console)
        : name_(name),
          port(port),
          console(console)
    {
      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
      MESSAGE_HANDLER(actors::msg::Timeout, timeout_handler);
    }

    void start_server()
    {
      context = zmq_ctx_new();
      responder = zmq_socket(context, ZMQ_REP);

      // Socket Timeouts - don't block forever on dead clients
      int timeout_ms = 10000;  // 10 seconds
      zmq_setsockopt(responder, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
      zmq_setsockopt(responder, ZMQ_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));

      // TCP Keepalive - detect dead connections
      int keepalive = 1;
      int keepalive_idle = 30;   // 30s idle before probing
      int keepalive_intvl = 5;   // probe every 5s
      int keepalive_cnt = 3;     // drop after 3 failed probes
      zmq_setsockopt(responder, ZMQ_TCP_KEEPALIVE, &keepalive, sizeof(keepalive));
      zmq_setsockopt(responder, ZMQ_TCP_KEEPALIVE_IDLE, &keepalive_idle, sizeof(keepalive_idle));
      zmq_setsockopt(responder, ZMQ_TCP_KEEPALIVE_INTVL, &keepalive_intvl, sizeof(keepalive_intvl));
      zmq_setsockopt(responder, ZMQ_TCP_KEEPALIVE_CNT, &keepalive_cnt, sizeof(keepalive_cnt));

      // Linger - don't wait on close
      int linger = 0;
      zmq_setsockopt(responder, ZMQ_LINGER, &linger, sizeof(linger));

      boost::format fmt("tcp://*:%d");
      std::string bind_addr = (fmt % port).str();

      int rc = zmq_bind(responder, bind_addr.c_str());
      if (rc != 0)
      {
        log_err("zmq_bind failed");
        perror("zmq_bind");
        std::cerr << "Error binding to address: " << bind_addr << std::endl;
        ERR("zmq_bind failed");
      }
      log_inf("MQ0_server started on %s", bind_addr);
    }

    std::optional<std::string> recv_message() const noexcept
    {
      zmq_msg_t message;
      if (zmq_msg_init(&message) != 0)
      {
        log_err("Failed to initialize ZMQ message");
        return {};
      }

      if (zmq_msg_recv(&message, responder, 0) == -1)
      {
        log_dbg("Failed to receive ZMQ message (timeout or no client)");
        zmq_msg_close(&message);
        return {};
      }

      char *msg = (char *)zmq_msg_data(&message);
      std::string msg_str(msg, zmq_msg_size(&message));

      if (zmq_msg_close(&message) != 0)
      {
        log_err("Failed to close ZMQ message");
        return {};
      }

      return msg_str;
    }

    void send_message(const std::string &msg) const noexcept
    {
      log_dbg("send_message: %s", msg);
      zmq_send(responder, msg.c_str(), msg.size(), 0);
    }

    void start_handler(const actors::msg::Start *)
    {
      log_inf("start_handler");
      start_server();
      send(new actors::msg::Continue(), 0);
    }

    void shutdown_handler(const actors::msg::Shutdown *)
    {
      log_inf("shutdown_handler");
    }

    void continue_handler(const actors::msg::Continue *)
    {
      log_dbg("continue_handler");
      auto pcm = new frame::cons::msg::Cmd;
      auto msg = recv_message();
      if (msg)
      {
        log_dbg("recv_message: %s", *msg);
        if (msg->size() >= frame::cons::msg::Cmd::buflen)
        {
          log_err("message too long");
          send_message("ERROR: message too long");
          send(new actors::msg::Continue(), 0);
          return;
        }
        else
        {
          memset(pcm->buf, 0, frame::cons::msg::Cmd::buflen);
          strncpy(pcm->buf, msg->c_str(), msg->size());

          log_dbg("got command: %s", pcm->buf);

          if (strncmp(pcm->buf, "quit", 4) == 0)
          {
            log_inf("quitting");
            send(new actors::msg::Shutdown(), 0);
            return;
          }
          else if (strncmp(pcm->buf, "pid", 3) == 0)
          {
            pid_t pid = getpid();
            char buf[200];
            memset(buf, 0, 200);
            sprintf(buf, "%d\n", pid);
            send_message(buf);
            send(new actors::msg::Continue(), 0);
            return;
          }
          else if (strncmp(pcm->buf, "time", 4) == 0)
          {
            std::string t = chutil::Time::now_utc().to_string();
            t.append("\n");
            send_message(t);
            send(new actors::msg::Continue(), 0);
            return;
          }
          else if (strncmp(pcm->buf, "abort", 5) == 0)
          {
            log_inf("aborting");
            exit(0);
          }
          else if (strncmp(pcm->buf, "msg", 3) == 0)
          {
            log_dbg("msg command received");
            std::stringstream ss;
            auto mgr = get_manager();
            if (mgr) {
              const auto &ml = mgr->get_message_counts();
              log_dbg("msg command: got %zu message types", ml.size());
              for(const auto & p : ml)
              {
                ss << p.first << "\t" << std::get<0>(p.second) << "\t" << std::get<1>(p.second) << std::endl;
              }
            } else {
              log_dbg("msg command: no manager");
            }
            auto rep = ss.str();
            log_dbg("msg command: sending %zu bytes", rep.size());
            send_message(rep);
            send(new actors::msg::Continue(), 0);
            return;
          }
          else if (strncmp(pcm->buf, "qlen", 4) == 0)
          {
            std::stringstream ss;
            auto mgr = get_manager();
            if (mgr) {
              const auto &ql = mgr->get_queue_lengths();
              for(const auto & p : ql)
              {
                ss << p.first << "\t" << p.second << std::endl;
              }
            }
            auto rep = ss.str();
            send_message(rep);
            send(new actors::msg::Continue(), 0);
            return;
          }
          else if (strncmp(pcm->buf, "free", 4) == 0)
          {
            std::stringstream ss;
            FILE *pipe = popen("free -h", "r");
            if (!pipe)
            {
              ss << "popen failed!";
            }
            else
            {
              char buffer[1024];
              while (fgets(buffer, sizeof buffer, pipe) != NULL)
              {
                ss << buffer;
              }
              pclose(pipe);
            }
            send_message(ss.str());
            send(new actors::msg::Continue(), 0);
            return;
          }
          else if (strncmp(pcm->buf, "uptime", 6) == 0)
          {
            std::stringstream ss;
            FILE *pipe = popen("uptime", "r");
            if (!pipe)
            {
              ss << "popen failed!";
            }
            else
            {
              char buffer[1024];
              while (fgets(buffer, sizeof buffer, pipe) != NULL)
              {
                ss << buffer;
              }
              pclose(pipe);
            }
            send_message(ss.str());
            send(new actors::msg::Continue(), 0);
            return;
          }
          else if (strncmp(pcm->buf, "ifconfig", 8) == 0)
          {
            std::stringstream ss;
            FILE *pipe = popen("ifconfig", "r");
            if (!pipe)
            {
              ss << "popen failed!";
            }
            else
            {
              char buffer[1024];
              while (fgets(buffer, sizeof buffer, pipe) != NULL)
              {
                ss << buffer;
              }
              pclose(pipe);
            }
            send_message(ss.str());
            send(new actors::msg::Continue(), 0);
            return;
          }
          else if (strncmp(pcm->buf, "sh", 2) == 0)
          {
            std::stringstream ss;
            // Convert pcm->buf to a std::string
            std::string str(pcm->buf);

            // Replace all occurrences of "__" with " " in str using Boost
            boost::replace_all(str, "__", " ");

            // Convert str back to a C-style string and store it in pcm->buf
            strncpy(pcm->buf, str.c_str(), sizeof(pcm->buf) - 1);
            pcm->buf[sizeof(pcm->buf) - 1] = '\0'; // Ensure null termination

            FILE *pipe = popen(pcm->buf + 3, "r");
            if (!pipe)
            {
              ss << "popen failed!";
            }
            else
            {
              char buffer[1024];
              while (fgets(buffer, sizeof buffer, pipe) != NULL)
              {
                ss << buffer;
              }
              pclose(pipe);
            }
            send_message(ss.str());
            send(new actors::msg::Continue(), 0);
            return;
          }
          else
          {
            log_dbg("sending command to console: %s", pcm->buf);
          }

          auto ret = console->fast_send((pcm), this);
          delete pcm;
          auto ptr = dynamic_cast<const frame::cons::msg::CmdR *>(ret.get());
          std::string res;
          if (ptr)
          {
            res = ptr->res;
          }
          else
          {
            res = "ERROR: no response";
            log_err("no response");
          }
          send_message(res);
          send(new actors::msg::Continue(), 0);
        }
      }
      else
      {
        log_dbg("recv_message failed (timeout or no client)");
        send(new actors::msg::Continue());
      }
    }

    void timeout_handler(const actors::msg::Timeout *)
    {
      log_inf("timeout_handler");
    }
  };
}
