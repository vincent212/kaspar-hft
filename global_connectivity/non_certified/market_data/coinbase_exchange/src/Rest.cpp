/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "coinbase_exchange/Rest.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "sim/WsNet.hpp"

namespace coinbase_exchange
{

using namespace sim::wsnet;

std::string https_get(const std::string &host, const std::string &target, int timeout_s, size_t body_limit)
{
  net::io_context io;
  auto tls = make_tls();
  beast::ssl_stream<beast::tcp_stream> s(io, tls);
  tls_connect(io, s, host, "443");
  http::request<http::string_body> req{http::verb::get, target, 11};
  req.set(http::field::host, host);
  req.set(http::field::user_agent, "kaspar-hft");
  req.set(http::field::accept, "application/json");
  beast::get_lowest_layer(s).expires_after(std::chrono::seconds(timeout_s));
  await(io, [&](auto h) { http::async_write(s, req, std::move(h)); });
  beast::flat_buffer buf;
  http::response_parser<http::string_body> parser;
  parser.body_limit(body_limit);
  beast::get_lowest_layer(s).expires_after(std::chrono::seconds(timeout_s));
  await(io, [&](auto h) { http::async_read(s, buf, parser, std::move(h)); });
  auto res = parser.release();
  if (res.result() != http::status::ok)
    throw std::runtime_error("GET " + host + target + ": HTTP " + std::to_string(res.result_int()) + " " +
                             res.body().substr(0, 200));
  beast::error_code ec;
  s.shutdown(ec); // servers often skip the TLS close_notify; not an error
  return std::move(res.body());
}

std::string base64_encode(const std::string &bytes)
{
  std::string out(4 * ((bytes.size() + 2) / 3) + 1, '\0');
  int n = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(out.data()),
                          reinterpret_cast<const unsigned char *>(bytes.data()), int(bytes.size()));
  out.resize(size_t(n));
  return out;
}

std::optional<std::string> base64_decode(const std::string &b64)
{
  if (b64.size() % 4 != 0)
    return std::nullopt;
  for (char c : b64)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '/' || c == '='))
      return std::nullopt;
  std::string out(3 * (b64.size() / 4) + 1, '\0');
  int n = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(out.data()),
                          reinterpret_cast<const unsigned char *>(b64.data()), int(b64.size()));
  if (n < 0)
    return std::nullopt;
  size_t pad = 0;
  if (!b64.empty() && b64.back() == '=')
    ++pad;
  if (b64.size() > 1 && b64[b64.size() - 2] == '=')
    ++pad;
  out.resize(size_t(n) - pad);
  return out;
}

std::optional<std::string> sign(const std::string &secret_b64, const std::string &message)
{
  auto key = base64_decode(secret_b64);
  if (!key)
    return std::nullopt;
  unsigned char mac[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  if (!HMAC(EVP_sha256(), key->data(), int(key->size()), reinterpret_cast<const unsigned char *>(message.data()),
            message.size(), mac, &len))
    return std::nullopt;
  return base64_encode(std::string(reinterpret_cast<char *>(mac), len));
}

std::string fix_logon_prehash(const std::string &sending_time, const std::string &msg_type, const std::string &seq_num,
                              const std::string &api_key, const std::string &target_comp_id,
                              const std::string &passphrase)
{
  const char soh = '\x01';
  return sending_time + soh + msg_type + soh + seq_num + soh + api_key + soh + target_comp_id + soh + passphrase;
}

const char *to_string(Environment e)
{
  return e == Environment::Sandbox ? "sandbox" : "production";
}

std::optional<Environment> parse_environment(const std::string &s)
{
  if (s == "sandbox")
    return Environment::Sandbox;
  if (s == "production")
    return Environment::Production;
  return std::nullopt;
}

std::string default_key_path()
{
  const char *home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/.kaspar/keys/coinbase_exchange_sandbox.json";
}

bool load_credentials(const std::string &path, Credentials &out, std::string &err)
{
  struct stat st{};
  if (::stat(path.c_str(), &st) != 0)
  {
    err = "no credentials file at " + path;
    return false;
  }
  if (st.st_mode & (S_IRWXG | S_IRWXO))
  {
    err = path + " is readable by group or others; chmod 600 it";
    return false;
  }
  std::ifstream f(path);
  nlohmann::json j;
  try
  {
    f >> j;
  }
  catch (const std::exception &)
  {
    err = path + " is not valid JSON";
    return false;
  }
  auto get = [&](const char *k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); };
  auto env = parse_environment(get("environment"));
  if (!env)
  {
    err = path + ": \"environment\" must be \"sandbox\" or \"production\"";
    return false;
  }
  out.environment = *env;
  out.key = get("key");
  out.secret = get("secret");
  out.passphrase = get("passphrase");
  out.oe_key = get("oe_key");
  out.oe_secret = get("oe_secret");
  out.oe_passphrase = get("oe_passphrase");
  out.dc_key = get("dc_key");
  out.dc_secret = get("dc_secret");
  out.dc_passphrase = get("dc_passphrase");
  if (out.key.empty() || out.secret.empty() || out.passphrase.empty())
  {
    err = path + ": \"key\", \"secret\" and \"passphrase\" are required";
    return false;
  }
  if (!base64_decode(out.secret))
  {
    err = path + ": \"secret\" is not base64";
    return false;
  }
  return true;
}

Endpoints endpoints(Environment e)
{
  Endpoints p;
  if (e == Environment::Sandbox)
  {
    p.rest_host = "api-public.sandbox.exchange.coinbase.com";
    p.ws_host = "ws-feed-public.sandbox.exchange.coinbase.com";
    p.ws_direct_host = "ws-direct.sandbox.exchange.coinbase.com";
    p.fix_ord_host = "fix-ord.sandbox.exchange.coinbase.com";
    p.fix_md_host = "fix-md.sandbox.exchange.coinbase.com";
    p.fix_dc_host = "fix-dc.sandbox.exchange.coinbase.com";
  }
  else
  {
    p.rest_host = "api.exchange.coinbase.com";
    p.ws_host = "ws-feed.exchange.coinbase.com";
    p.ws_direct_host = "ws-direct.exchange.coinbase.com";
    p.fix_ord_host = "fix-ord.exchange.coinbase.com";
    p.fix_md_host = "fix-md.exchange.coinbase.com";
    p.fix_dc_host = "fix-dc.exchange.coinbase.com";
  }
  return p;
}

bool is_sandbox_host(const std::string &host)
{
  static const std::string suffix = ".sandbox.exchange.coinbase.com";
  return host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool host_allowed(Environment configured, const std::string &host, const Credentials *creds, std::string &err)
{
  const bool sandbox_host = is_sandbox_host(host);
  if (configured == Environment::Sandbox && !sandbox_host)
  {
    err = "configured for the sandbox but " + host + " is not a sandbox host: refusing to connect";
    return false;
  }
  if (configured == Environment::Production && sandbox_host)
  {
    err = "configured for production but " + host + " is a sandbox host";
    return false;
  }
  if (creds && creds->environment != configured)
  {
    err = std::string("the credentials are for ") + to_string(creds->environment) + ", the handler is configured for " +
          to_string(configured) + ": refusing to connect";
    return false;
  }
  return true;
}

}
