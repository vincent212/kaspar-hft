/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LMAX FIX sessions: what the market-data handler, the order manager and the
// post-trade client share. NOT CERTIFIED.
//
//   - the environment guard: a session configured for LMAX's demo accepts only
//     a *.london-demo.lmax.com host, and so on; production is refused outright
//     by this build (feed.md section 2)
//   - the key file (credentials and CompIDs), read from outside the repository
//   - the QuickFIX settings for one session, built in memory so that no
//     settings file with credentials is ever written
//   - FIX time and number helpers

#include <cstdint>
#include <string>

namespace lmax::fix
{
  enum class Env
  {
    Loopback,   // the in-process simulator on 127.0.0.1 (tests, --sim)
    Demo,       // LMAX London demo: *.london-demo.lmax.com
    Uat,        // LMAX broker UAT: *.london-uat.lmax.com
    Production, // refused: nothing here is certified
  };
  const char *to_string(Env e);
  bool parse_env(const std::string &s, Env &out);

  // True when `host` belongs to `env`; otherwise false and the reason in why.
  // A name is compared case-insensitively and must end in the environment's
  // domain; a numeric address is accepted only for Loopback (127.0.0.1).
  bool host_allowed(Env env, const std::string &host, std::string &why);

  // The key file, e.g. ~/.kaspar/keys/lmax_demo.json (README, "Live check"):
  //   {
  //     "environment": "demo",
  //     "username": "...", "password": "...",
  //     "md_sender_comp_id": "...", "md_target_comp_id": "...",
  //     "trade_sender_comp_id": "...", "trade_target_comp_id": "...",
  //     "account": ""          (optional: LMAX account id, tag 1)
  //   }
  // SenderCompID defaults to the username where a *_sender_comp_id is absent.
  struct Credentials
  {
    Env env = Env::Demo;
    std::string username, password;
    std::string md_sender, md_target;
    std::string trade_sender, trade_target;
    std::string account;
  };
  bool load_credentials(const std::string &path, Credentials &out, std::string &err);

  // One QuickFIX initiator session.
  struct SessionParams
  {
    Env env = Env::Demo;
    std::string host;
    int port = 443;
    bool tls = true;                 // LMAX: TLS on every internet endpoint
    std::string sender, target;      // CompIDs
    int heartbeat_s = 30;
    bool reset_on_logon = true;      // 141=Y on every logon
    std::string dictionary;          // QuickFIX DataDictionary path
    std::string store_path;          // FileStorePath; empty: memory store
    std::string log_path;            // FileLogPath; empty: no FIX log
    int reconnect_s = 5;
    // Session schedule in America/New_York (feed.md section 2): the market
    // data and trading gateways open and close at 17:02:00 New York time.
    std::string start_time = "17:02:00";
    std::string end_time = "17:01:00";
    std::string start_day; // empty: a daily session; "Sat": weekly from Saturday
    std::string end_day;
  };

  // QuickFIX settings text for p (one [DEFAULT] and one [SESSION]). Checks the
  // host against the environment first: false with the reason in err.
  bool settings_text(const SessionParams &p, std::string &out, std::string &err);

  // UTCTimestamp "YYYYMMDD-HH:MM:SS[.sss[sss[sss]]]" to nanoseconds since the
  // epoch; 0 if malformed.
  uint64_t utc_timestamp_ns(const std::string &s);
  // UTCDateOnly "YYYYMMDD" + UTCTimeOnly "HH:MM:SS[.sss]" to ns since epoch.
  uint64_t utc_date_time_ns(const std::string &date, const std::string &time);
  // Nanoseconds since the epoch, now.
  uint64_t now_ns();

  // A decimal FIX value scaled by 10^decimals and rounded; false if it is not
  // a number or does not land on the grid within 1e-6 of a step.
  bool to_steps(double value, double step, int64_t &out);
}
