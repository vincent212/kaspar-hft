/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Signing, the request bodies, the host guard and the key file.
//
// Bybit's guide (v5/guide, "Create A Request") gives two worked examples: the
// strings to sign and the resulting signatures, but not the secret they were
// made with (api_key "XXXXXXXXXX", the secret is not shown). The strings are
// checked exactly here; the signatures cannot be. The HMAC itself is checked
// against RFC 4231 test case 2 and against Python's hmac module on the
// documentation's strings with a test secret.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>

#include "bybit_oe/BybitAuth.hpp"
#include "bybit_oe/BybitPrivate.hpp"
#include "bybit_oe/BybitSession.hpp"
#include "bybit_oe/act/BybitOM.hpp"

using namespace bybit;

TEST(BybitAuth, HmacSha256Rfc4231Case2)
{
  EXPECT_EQ(auth::hmac_sha256_hex("Jefe", "what do ya want for nothing?"),
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(BybitAuth, GuideGetExampleStringToSign)
{
  // timestamp 1658384314791, api_key XXXXXXXXXX, recv_window 5000,
  // queryString category=option&symbol=BTC-29JUL22-25000-C
  EXPECT_EQ(auth::rest_prehash(1658384314791ull, "XXXXXXXXXX", 5000,
                               "category=option&symbol=BTC-29JUL22-25000-C"),
            "1658384314791XXXXXXXXXX5000category=option&symbol=BTC-29JUL22-25000-C");
  // Python: hmac.new(b"kaspar-test-secret", <string>, sha256).hexdigest()
  auth::Credentials c{"XXXXXXXXXX", "kaspar-test-secret"};
  EXPECT_EQ(auth::rest_sign(c, 1658384314791ull, 5000, "category=option&symbol=BTC-29JUL22-25000-C"),
            "80d8583e73bc4de57d5644cfaafb726effc52eec9a3b354b3a1698e182b5dd68");
}

TEST(BybitAuth, GuidePostExampleStringToSign)
{
  EXPECT_EQ(auth::rest_prehash(1658385579423ull, "XXXXXXXXXX", 5000, R"({"category": "option"})"),
            R"(1658385579423XXXXXXXXXX5000{"category": "option"})");
  auth::Credentials c{"XXXXXXXXXX", "kaspar-test-secret"};
  EXPECT_EQ(auth::rest_sign(c, 1658385579423ull, 5000, R"({"category": "option"})"),
            "dde06a53a553f40fd95420b61af0a4086179dbeb94c84f163ac4301e926e01e1");
}

TEST(BybitAuth, WebSocketAuthFrame)
{
  // v5/ws/connect: signature = HMAC(secret, "GET/realtime" + expires).
  EXPECT_EQ(auth::ws_prehash(1662350400000ull), "GET/realtime1662350400000");
  auth::Credentials c{"KEY", "kaspar-test-secret"};
  auto j = nlohmann::json::parse(auth::ws_auth_frame(c, 1662350400000ull));
  EXPECT_EQ(j["op"], "auth");
  EXPECT_EQ(j["args"][0], "KEY");
  EXPECT_EQ(j["args"][1], 1662350400000ull);
  EXPECT_EQ(j["args"][2], "16e707f8abc45f17ee69b4f1e2bc77df42b2d1eda50dae023b896aca950ca15b");
}

// The body is signed byte for byte as sent.
TEST(BybitAuth, CreateBodyIsWhatIsSigned)
{
  oe::PlaceRequest p;
  p.category = "linear";
  p.symbol = "BTCUSDT";
  p.side = "Buy";
  p.order_type = "Limit";
  p.qty = "0.001";
  p.price = "10000.0";
  p.time_in_force = "GTC";
  p.order_link_id = "k20261118-42";
  const auto body = p.body();
  EXPECT_EQ(body, R"({"category":"linear","symbol":"BTCUSDT","side":"Buy","orderType":"Limit","qty":"0.001",)"
                  R"("price":"10000.0","timeInForce":"GTC","orderLinkId":"k20261118-42","positionIdx":0})");
  auth::Credentials c{"XXXXXXXXXX", "kaspar-test-secret"};
  EXPECT_EQ(auth::rest_sign(c, 1700000000000ull, 5000, body),
            "a1a416202a5cdc7d89aed3e22045b79cc6855762be9f3b339465655372c0d481");
  oe::PlaceRequest s = p;
  s.category = "spot";
  s.order_type = "Market";
  s.price.clear();
  s.time_in_force = "IOC";
  EXPECT_EQ(s.body(), R"({"category":"spot","symbol":"BTCUSDT","side":"Buy","orderType":"Market","qty":"0.001",)"
                      R"("timeInForce":"IOC","orderLinkId":"k20261118-42"})");
  oe::CancelRequest x{"linear", "BTCUSDT", "k20261118-42"};
  EXPECT_EQ(x.body(), R"({"category":"linear","symbol":"BTCUSDT","orderLinkId":"k20261118-42"})");
}

TEST(BybitAuth, HostGuardRefusesProduction)
{
  EXPECT_NO_THROW(priv::guard(true, "api-testnet.bybit.com", "stream-testnet.bybit.com"));
  EXPECT_NO_THROW(priv::guard(true, "api-demo.bybit.com", "stream-demo.bybit.com"));
  EXPECT_THROW(priv::guard(true, "api.bybit.com", "stream-testnet.bybit.com"), std::runtime_error);
  EXPECT_THROW(priv::guard(true, "api-testnet.bybit.com", "stream.bybit.com"), std::runtime_error);
  EXPECT_THROW(priv::guard(true, "api.bytick.com", "stream.bybit.com"), std::runtime_error);
  EXPECT_THROW(priv::guard(true, "api.bybit.kz", "stream.bybit.kz"), std::runtime_error);
  EXPECT_THROW(priv::RestClient({"k", "s"}, "api.bybit.com", true), std::runtime_error);
  EXPECT_THROW(priv::PrivateWs({"k", "s"}, "stream.bybit.com", true), std::runtime_error);
  // The real session refuses too, before connecting.
  oe::SessionConfig sc;
  sc.rest_host = "api.bybit.com";
  struct Nop : oe::SessionEvents
  {
    void logged_in() override {}
    void login_rejected(const std::string &) override {}
    void disconnected(const std::string &) override {}
    void busy() override {}
    void available() override {}
    void order(const std::string &) override {}
    void execution(const std::string &) override {}
    void response(oe::RequestKind, const std::string &, int, const std::string &) override {}
  } nop;
  EXPECT_THROW(oe::rest_ws_session_factory()(sc, nop), std::runtime_error);
}

TEST(BybitAuth, KeyFile)
{
  const std::string path = "/tmp/bybit_test_key_" + std::to_string(::getpid()) + ".json";
  {
    std::ofstream o(path);
    o << R"({"api_key": "AAA", "api_secret": "BBB"})";
  }
  auth::Credentials c;
  std::string err;
  ASSERT_TRUE(auth::load(path, c, err)) << err;
  EXPECT_EQ(c.api_key, "AAA");
  {
    std::ofstream o(path);
    o << R"({"api_key": "AAA"})";
  }
  EXPECT_FALSE(auth::load(path, c, err));
  EXPECT_EQ(err.find("BBB"), std::string::npos);
  std::remove(path.c_str());
  EXPECT_FALSE(auth::load("/nonexistent/bybit.json", c, err));
}

TEST(BybitAuth, LinkIdsAndOrderIds)
{
  using oe::BybitOM;
  const auto l = BybitOM::link_id(20261118, 0x7fffffffffffffffull);
  EXPECT_LE(l.size(), 36u);
  EXPECT_EQ(l, "k20261118-9223372036854775807");
  frame::som::ClientId cid = 0;
  ASSERT_TRUE(BybitOM::parse_link(l, cid));
  EXPECT_EQ(cid, 0x7fffffffffffffffull);
  EXPECT_FALSE(BybitOM::parse_link("spot-test-postonly", cid));
  EXPECT_FALSE(BybitOM::parse_link("k20261118-12x", cid));
  // Spot orderIds are decimal (create-order example); linear ones are UUIDs.
  EXPECT_EQ(BybitOM::xordid("1321003749386327552"), 1321003749386327552ull);
  EXPECT_NE(BybitOM::xordid("c6f055d9-7f21-4079-913d-e6523a9cfffa"), 0u);
  EXPECT_NE(BybitOM::xordid("c6f055d9-7f21-4079-913d-e6523a9cfffa"),
            BybitOM::xordid("c6f055d9-7f21-4079-913d-e6523a9cfffb"));
  EXPECT_EQ(BybitOM::decimal(63169.1, 0.1), "63169.1");
  EXPECT_EQ(BybitOM::decimal(0.003, 0.001), "0.003");
  EXPECT_EQ(BybitOM::decimal(0.000123, 0.000001), "0.000123");
  EXPECT_EQ(BybitOM::decimal(64000, 1), "64000");
}
