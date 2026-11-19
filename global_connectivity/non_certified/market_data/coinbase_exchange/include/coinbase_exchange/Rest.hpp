#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// HTTPS GET, the Coinbase Exchange signature (HMAC-SHA256 with the
// base64-decoded secret, base64 output), the credentials file, and the
// endpoint table with its sandbox/production check. Used by the market-data
// feed, the order manager's Logon and the drop-copy client's Logon.

#include <cstdint>
#include <optional>
#include <string>

namespace coinbase_exchange
{
  // GET https://host/target. Throws on a network error or a status other than
  // 200 (the message has the status and the start of the body).
  std::string https_get(const std::string &host, const std::string &target, int timeout_s = 30,
                        size_t body_limit = 128u << 20);

  std::string base64_encode(const std::string &bytes);
  // nullopt if `b64` is not valid base64
  std::optional<std::string> base64_decode(const std::string &b64);

  // base64(HMAC-SHA256(base64decode(secret_b64), message)); nullopt if the
  // secret is not valid base64.
  std::optional<std::string> sign(const std::string &secret_b64, const std::string &message);

  // The prehash strings Coinbase signs.
  //   REST and the WebSocket subscribe: timestamp + METHOD + path + body
  //   FIX Logon: SendingTime, MsgType, MsgSeqNum, SenderCompID, TargetCompID,
  //              Passphrase joined by SOH (0x01), no trailing separator
  inline std::string rest_prehash(const std::string &ts, const std::string &method, const std::string &path,
                                  const std::string &body)
  {
    return ts + method + path + body;
  }
  std::string fix_logon_prehash(const std::string &sending_time, const std::string &msg_type,
                                const std::string &seq_num, const std::string &api_key,
                                const std::string &target_comp_id, const std::string &passphrase);

  enum class Environment
  {
    Sandbox,
    Production,
  };
  const char *to_string(Environment e);
  std::optional<Environment> parse_environment(const std::string &s);

  // API credentials, read at run time from a JSON file outside the repository
  // (default ~/.kaspar/keys/coinbase_exchange_sandbox.json):
  //   { "environment": "sandbox", "key": "...", "secret": "<base64>",
  //     "passphrase": "...", "fix_key": "...", ... }
  // Never logged; to_string() of a Credentials does not exist on purpose.
  struct Credentials
  {
    Environment environment = Environment::Sandbox;
    std::string key, secret, passphrase;
    // Separate API keys for the FIX sessions (Coinbase allows one connection
    // per key); empty: use `key`/`secret`/`passphrase`.
    std::string oe_key, oe_secret, oe_passphrase;
    std::string dc_key, dc_secret, dc_passphrase;
  };
  // false with the reason in err (never the file's contents). The file must
  // not be readable by group or others.
  bool load_credentials(const std::string &path, Credentials &out, std::string &err);
  std::string default_key_path();
  // "~" or "~/..." with $HOME in place of the "~" (unchanged if HOME is not
  // set, or the path does not start that way): the configuration files'
  // paths.
  std::string expand_home(const std::string &path);

  // Hosts of one environment (docs: exchange/introduction/sandbox,
  // exchange/websocket-feed/overview, exchange/fix-api/connectivity).
  struct Endpoints
  {
    std::string rest_host;      // api.exchange.coinbase.com
    std::string ws_host;        // ws-feed.exchange.coinbase.com (public)
    std::string ws_direct_host; // ws-direct.exchange.coinbase.com (authenticated)
    std::string fix_ord_host;   // fix-ord.exchange.coinbase.com:6121
    std::string fix_md_host;    // fix-md.exchange.coinbase.com:6121
    std::string fix_dc_host;    // fix-dc.exchange.coinbase.com:6122
    int fix_ord_port = 6121, fix_md_port = 6121, fix_dc_port = 6122;
  };
  Endpoints endpoints(Environment e);

  // true if `host` belongs to the sandbox (*.sandbox.exchange.coinbase.com).
  bool is_sandbox_host(const std::string &host);
  // The guard every handler runs before connecting: a sandbox configuration
  // may only reach sandbox hosts, and credentials may only go to a host of
  // their own environment. false with the reason in err.
  bool host_allowed(Environment configured, const std::string &host, const Credentials *creds, std::string &err);
}
