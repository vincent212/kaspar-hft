/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Signing (docs-v5 "REST Authentication", "WebSocket / Login") and the
// environment guard (docs-v5 "Demo Trading Services").
//
// The documentation's own signature examples ("7L+zFQ+CEgGu5rzCj4+BdV2/uUHG
// qddA9pI6ztsRRPs=" for the WebSocket login, "leaVRETrtaoEQ3yI9qEtI1CZ82ikZ4x
// SG5Kj8gnl3uw=" in the demo header example) are given without the secret
// that produced them, so they cannot be reproduced. The tests check the
// documented pre-hash string, HMAC-SHA256 against RFC 4231, and the full
// signature against Python's hmac/hashlib/base64 for the documentation's
// example SecretKey (22582BD0CFF14C41EDBF1AB98506286D) and example inputs.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>

#include "okx_oe/OkxAuth.hpp"
#include "okx_oe/OkxConfig.hpp"

using namespace okx;
using namespace okx::oe;

namespace
{
  const std::string kDocSecret = "22582BD0CFF14C41EDBF1AB98506286D";
}

TEST(OkxAuth, HmacSha256Base64MatchesRfc4231Case2)
{
  // RFC 4231 4.3: HMAC-SHA-256("Jefe", "what do ya want for nothing?") =
  // 5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843
  EXPECT_EQ(auth::hmac_sha256_base64("Jefe", "what do ya want for nothing?"),
            "W9zBRr9gdU5qBCQmCJV1x1oAPwidJzmDnexYuWTsOEM=");
}

TEST(OkxAuth, PrehashIsTimestampMethodPathBody)
{
  // docs-v5: sign = Base64(HmacSHA256(timestamp + 'GET' + '/api/v5/account/balance?ccy=BTC', SecretKey))
  EXPECT_EQ(auth::prehash("2020-12-08T09:08:57.715Z", "GET", "/api/v5/account/balance?ccy=BTC", ""),
            "2020-12-08T09:08:57.715ZGET/api/v5/account/balance?ccy=BTC");
  EXPECT_EQ(auth::prehash("2020-12-08T09:08:57.715Z", "POST", "/api/v5/account/set-leverage",
                          R"({"instId":"BTC-USDT","lever":"5","mgnMode":"isolated"})"),
            R"(2020-12-08T09:08:57.715ZPOST/api/v5/account/set-leverage{"instId":"BTC-USDT","lever":"5","mgnMode":"isolated"})");
}

TEST(OkxAuth, RestSignaturesMatchAnIndependentImplementation)
{
  // Python: base64.b64encode(hmac.new(secret, prehash, hashlib.sha256).digest())
  EXPECT_EQ(auth::rest_sign(kDocSecret, "2020-03-28T12:21:41.274Z", "GET", "/api/v5/account/balance?ccy=BTC", ""),
            "nhRd1FR0Yl5WTy1IdfJxJmS4t2NjQSf6v5FC2CnFa9c=");
  EXPECT_EQ(auth::rest_sign(kDocSecret, "2020-12-08T09:08:57.715Z", "POST", "/api/v5/account/set-leverage",
                            R"({"instId":"BTC-USDT","lever":"5","mgnMode":"isolated"})"),
            "eCnnCgWLjlQ9XnpUkrcny3qNq3WW/81KNrDr/XR6Xv8=");
}

TEST(OkxAuth, WebSocketLoginUsesUnixSecondsAndUsersSelfVerify)
{
  auth::Keys k{"key-id", kDocSecret, "pass"};
  auto j = auth::ws_login(k, 1538054050);
  EXPECT_EQ(j["op"], "login");
  ASSERT_EQ(j["args"].size(), 1u);
  const auto &a = j["args"][0];
  EXPECT_EQ(a["apiKey"], "key-id");
  EXPECT_EQ(a["passphrase"], "pass");
  EXPECT_EQ(a["timestamp"], "1538054050");
  // Python over "1538054050GET/users/self/verify" with the doc's SecretKey.
  EXPECT_EQ(a["sign"], "+LdIr8lkkvhr5hoA3g9TMC0+uQJ849ftAcocA/ouu4M=");
}

TEST(OkxAuth, IsoTimestampHasMillisecondsAndZ)
{
  EXPECT_EQ(auth::iso_timestamp(1607418537715ull), "2020-12-08T09:08:57.715Z"); // the docs' example
  EXPECT_EQ(auth::iso_timestamp(1607418537005ull), "2020-12-08T09:08:57.005Z");
}

TEST(OkxAuth, KeyFileErrorsNeverContainTheFile)
{
  const std::string p = "build/okx_test_keys.json";
  {
    std::ofstream(p) << R"({"api_key":"AKEY-SECRETLOOKING","secret_key":"","passphrase":"PW"})";
  }
  auth::Keys k;
  std::string err;
  EXPECT_FALSE(auth::load_keys(p, k, err));
  EXPECT_EQ(err.find("AKEY"), std::string::npos);
  EXPECT_EQ(err.find("PW"), std::string::npos);
  {
    std::ofstream(p) << R"({"api_key":"a","secret_key":"b","passphrase":"c"})";
  }
  EXPECT_TRUE(auth::load_keys(p, k, err));
  EXPECT_TRUE(k.complete());
  std::remove(p.c_str());
  EXPECT_FALSE(auth::load_keys("/nonexistent/okx.json", k, err));
}

// ---- environment guard ----------------------------------------------------------------------

TEST(OkxEnvironment, DemoRequiresTheDemoWebSocketHost)
{
  Endpoint ep;
  ep.demo = true;
  std::string err;
  ep.ws_host = "wspap.okx.com";
  EXPECT_TRUE(ep.check(err)) << err;
  for (const char *prod : {"ws.okx.com", "wsaws.okx.com", "wsus.okx.com", "example.com"})
  {
    ep.ws_host = prod;
    EXPECT_FALSE(ep.check(err)) << prod;
  }
}

TEST(OkxEnvironment, DemoRequestsCarryTheSimulatedTradingHeaderOrAreRefused)
{
  Endpoint ep;
  ep.demo = true;
  auto h = ep.headers();
  ASSERT_EQ(h.count("x-simulated-trading"), 1u);
  EXPECT_EQ(h["x-simulated-trading"], "1");
  std::string err;
  EXPECT_TRUE(require_demo_header(ep, h, err));
  h.erase("x-simulated-trading");
  EXPECT_FALSE(require_demo_header(ep, h, err));
  h["x-simulated-trading"] = "0";
  EXPECT_FALSE(require_demo_header(ep, h, err));
}

TEST(OkxEnvironment, ProductionIsRefusedUnlessAllowedAndNeverOnTheDemoHost)
{
  Endpoint ep;
  ep.demo = false;
  ep.ws_host = "ws.okx.com";
  std::string err;
  EXPECT_FALSE(ep.check(err));
  EXPECT_NE(err.find("allow_production"), std::string::npos);
  ep.allow_production = true;
  EXPECT_TRUE(ep.check(err)) << err;
  EXPECT_EQ(ep.headers().count("x-simulated-trading"), 0u);
  ep.ws_host = "wspap.okx.com";
  EXPECT_FALSE(ep.check(err));
}

TEST(OkxConfig, ExampleFileParsesAndValidates)
{
  OkxOMConfig c;
  std::string err;
  ASSERT_TRUE(OkxOMConfig::from_file("config/okx_demo_example.ini", c, err)) << err;
  EXPECT_TRUE(c.ep.demo);
  EXPECT_EQ(c.ep.ws_host, "wspap.okx.com");
  ASSERT_EQ(c.instruments.size(), 2u);
  EXPECT_EQ(c.instruments[0].inst_id_code, 3);
  EXPECT_EQ(c.instruments[1].inst_id_code, 2021032601102993LL); // demo code: does not fit in 32 bits
  EXPECT_EQ(c.instruments[1].tick, "0.01");
  EXPECT_TRUE(c.valid(err)) << err;
  c.ep.ws_host = "ws.okx.com";
  EXPECT_FALSE(c.valid(err));
  c.ep.ws_host = "wspap.okx.com";
  c.cancel_all_after_s = 5;
  EXPECT_FALSE(c.valid(err));
}

TEST(OkxConfig, DecimalScaling)
{
  EXPECT_EQ(decimals("0.0001"), 4);
  EXPECT_EQ(decimals("0.10"), 1);
  EXPECT_EQ(decimals("1"), 0);
  EXPECT_EQ(scaled(3, "0.0001"), "0.0003");
  EXPECT_EQ(scaled(631234, "0.1"), "63123.4");
  EXPECT_EQ(scaled(150, "0.01"), "1.50");
  bool exact = false;
  EXPECT_EQ(units_of("0.0003", "0.0001", exact), 3);
  EXPECT_TRUE(exact);
  EXPECT_EQ(units_of("63123.4", "0.1", exact), 631234);
  EXPECT_TRUE(exact);
  const auto half = units_of("0.00015", "0.0001", exact); // 1.5 units: not representable
  EXPECT_TRUE(half == 1 || half == 2);
  EXPECT_FALSE(exact);
}
