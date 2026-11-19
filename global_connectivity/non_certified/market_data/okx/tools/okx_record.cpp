/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// okx_record: writes OKX public WebSocket frames, exactly as received, to a
// file, with REST order-book snapshots interleaved, for replay and tests.
//
//   okx_record --inst BTC-USDT [--inst BTC-USDT-SWAP ...] --out FILE
//              [--seconds N] [--rest-every S] [--channel books]
//
// File format, one record per line:
//   W <recv_ns> <frame>                  a WebSocket text frame
//   R <recv_ns> <instId> <body>          GET /api/v5/market/books?sz=400 body
//   C <recv_ns> <text>                   a connection event (connect, error)
// OKX frames are single-line JSON; a frame containing a newline is written
// with the newline replaced by a space.
//
// Needs no account. REST snapshots default to one every 10 s per instrument
// (the endpoint allows 40 requests per 2 s per IP).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "okx_md/OkxWs.hpp"

namespace
{
  std::string one_line(std::string s)
  {
    for (auto &c : s)
      if (c == '\n' || c == '\r')
        c = ' ';
    return s;
  }
}

int main(int argc, char **argv)
{
  std::vector<std::string> insts;
  std::string out, channel = "books";
  int seconds = 60, rest_every = 10;
  for (int i = 1; i < argc; ++i)
  {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc)
      {
        std::cerr << a << " needs a value\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--inst")
      insts.push_back(next());
    else if (a == "--out")
      out = next();
    else if (a == "--seconds")
      seconds = std::stoi(next());
    else if (a == "--rest-every")
      rest_every = std::stoi(next());
    else if (a == "--channel")
      channel = next();
    else
    {
      std::cerr << "unknown argument " << a << "\n";
      return 2;
    }
  }
  if (insts.empty() || out.empty())
  {
    std::cerr << "usage: okx_record --inst ID [--inst ID] --out FILE [--seconds N] [--rest-every S]\n";
    return 2;
  }
  std::ofstream f(out);
  if (!f)
  {
    std::cerr << "cannot write " << out << "\n";
    return 1;
  }
  std::mutex mtx;
  auto write = [&](const std::string &line) {
    std::lock_guard<std::mutex> lk(mtx);
    f << line << '\n';
  };

  const uint64_t end = okx::now_ns() + uint64_t(seconds) * 1000000000ull;
  std::atomic<bool> stop{false};

  std::thread rest([&] {
    uint64_t next = okx::now_ns() + 2000000000ull;
    while (!stop)
    {
      if (okx::now_ns() >= next && rest_every > 0)
      {
        next += uint64_t(rest_every) * 1000000000ull;
        for (const auto &id : insts)
        {
          try
          {
            auto r = okx::https_request("www.okx.com", "GET", "/api/v5/market/books?instId=" + id + "&sz=400");
            write("R " + std::to_string(okx::now_ns()) + " " + id + " " + one_line(r.body));
          }
          catch (const std::exception &e)
          {
            write("C " + std::to_string(okx::now_ns()) + " rest error " + e.what());
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });

  uint64_t frames = 0;
  while (okx::now_ns() < end)
  {
    try
    {
      okx::WsClient ws("ws.okx.com", "443", "/ws/v5/public");
      ws.connect();
      nlohmann::json args = nlohmann::json::array();
      for (const auto &id : insts)
      {
        args.push_back({{"channel", channel}, {"instId", id}});
        args.push_back({{"channel", "trades"}, {"instId", id}});
      }
      ws.send(nlohmann::json{{"op", "subscribe"}, {"args", args}}.dump());
      write("C " + std::to_string(okx::now_ns()) + " connected");
      while (okx::now_ns() < end)
      {
        auto s = ws.read();
        write("W " + std::to_string(okx::now_ns()) + " " + one_line(s));
        ++frames;
      }
    }
    catch (const std::exception &e)
    {
      write("C " + std::to_string(okx::now_ns()) + " ws error " + e.what());
      std::cerr << "okx_record: " << e.what() << "; reconnecting in 1 s\n";
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
  stop = true;
  rest.join();
  std::cerr << "okx_record: " << frames << " frames to " << out << "\n";
  return 0;
}
