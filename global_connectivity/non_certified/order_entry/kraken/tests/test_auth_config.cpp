/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Request signing against the documentation's examples, the key file, and
// the testnet guard of the configuration.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>

#include "kraken_oe/KrakenAuth.hpp"
#include "kraken_oe/KrakenConfig.hpp"
#include "kraken_oe/KrakenSessions.hpp"

using namespace kraken::oe;

// exchange/guides/rest/authentication, "Setting the API-Sign Parameter":
// the documented example and its documented signature.
TEST(KrakenAuth, SpotApiSignMatchesDocumentationExample)
{
  EXPECT_EQ(spot_api_sign("/0/private/AddOrder", "1616492376594",
                          "nonce=1616492376594&ordertype=limit&pair=XBTUSD&price=37500&type=buy&volume=1.25",
                          "kQH5HW/8p1uGOVjbgWA7FunAmGO8lsSUXNsu3eow76sz84Q18fWxnyRzBHCd3pd5nE9qa99HAZtuZuj6F1huXg=="),
            "4/dpxb3iT4tp/ZCVEwSnEsLxx0bqyhLpdfOpc6fn7OR8+UClSV5n9E6aSS8MPtnRfp32bAb0nmbRn6H8ndwLUQ==");
}

// exchange/guides/futures/websockets, "Sign challenge", "Example".
TEST(KrakenAuth, FuturesChallengeMatchesDocumentationExample)
{
  EXPECT_EQ(futures_sign_challenge("c100b894-1729-464d-ace1-52dbce11db42",
                                   "7zxMEF5p/Z8l2p2U7Ghv6x14Af+Fx+92tPgUdVQ748FOIrEoT9bgT+bTRfXc5pz8na+hL/QdrCVG7bh9KpT0eMTm"),
            "4JEpF3ix66GA2B+ooK128Ift4XQVtc137N9yeg4Kqsn9PI0Kpzbysl9M1IeCEdjg0zl00wkVqcsnG4bmnlMb3A==");
}

// exchange/guides/futures/rest gives the Authent steps but no complete
// example (its example api_secret is 87 characters, not valid base64). The
// expected value was computed independently with Python's hashlib/hmac from
// the documented steps: SHA256(postData + nonce + endpointPath), HMAC-SHA512
// with the base64-decoded secret, base64.
TEST(KrakenAuth, FuturesAuthentMatchesIndependentComputation)
{
  const std::string secret =
      "7zxMEF5p/Z8l2p2U7Ghv6x14Af+Fx+92tPgUdVQ748FOIrEoT9bgT+bTRfXc5pz8na+hL/QdrCVG7bh9KpT0eMTm";
  const std::string pd = "orderType=lmt&symbol=PF_XBTUSD&side=buy&size=0.0001&limitPrice=10000&cliOrdId=abc";
  EXPECT_EQ(futures_authent(pd, "1731000000000", "/api/v3/sendorder", secret),
            "ArSW92usSTX7a07RISzNrolviZz0+5vR7YIZQ4t1sZ+o1iAIq5SxW3NzX5IXEIDxhoYicbZAVE5qxvJjdm4BZQ==");
  auto h = futures_headers("PUBKEY", secret, "POST", "/api/v3/sendorder", pd, "1731000000000");
  ASSERT_EQ(h.size(), 4u);
  EXPECT_EQ(h[0].first, "APIKey");
  EXPECT_EQ(h[1].first, "Nonce");
  EXPECT_EQ(h[2].first, "Authent");
  EXPECT_EQ(h[2].second, "ArSW92usSTX7a07RISzNrolviZz0+5vR7YIZQ4t1sZ+o1iAIq5SxW3NzX5IXEIDxhoYicbZAVE5qxvJjdm4BZQ==");
}

TEST(KrakenAuth, MalformedSecretThrowsAndNoncesIncrease)
{
  EXPECT_THROW(spot_api_sign("/0/private/X", "1", "nonce=1", "not base64!"), std::invalid_argument);
  auto a = next_nonce(), b = next_nonce(), c = next_nonce();
  EXPECT_LT(a, b);
  EXPECT_LT(b, c);
  EXPECT_EQ(url_encode("a b&c=d/é"), "a%20b%26c%3Dd%2F%C3%A9");
}

TEST(KrakenAuth, KeyFileErrorsNeverContainTheKey)
{
  const std::string p = "build/kraken_test_keys.json";
  Keys k;
  std::string err;
  EXPECT_FALSE(load_keys("/nonexistent/kraken.json", k, err));
  std::ofstream(p) << R"({"api_key":"SECRETKEYTEXT","api_secret":"not base64!"})";
  EXPECT_FALSE(load_keys(p, k, err));
  EXPECT_EQ(err.find("SECRETKEYTEXT"), std::string::npos);
  EXPECT_EQ(err.find("not base64!"), std::string::npos);
  std::ofstream(p) << R"({"api_key":"K","api_secret":"a2V5"})";
  EXPECT_TRUE(load_keys(p, k, err)) << err;
  EXPECT_EQ(k.api_key, "K");
  std::remove(p.c_str());
}

// ---- the testnet guard --------------------------------------------------------------------

TEST(KrakenConfig, TestnetAcceptsOnlyTheFuturesDemoHost)
{
  std::string err;
  Endpoint e; // futures, testnet, demo host
  EXPECT_TRUE(e.check(err)) << err;
  e.rest_host = "futures.kraken.com";
  EXPECT_FALSE(e.check(err));
  EXPECT_NE(err.find("refused"), std::string::npos);
  e.rest_host = "demo-futures.kraken.com.evil.example";
  EXPECT_FALSE(e.check(err));
  e.rest_host = "api.kraken.com";
  EXPECT_FALSE(e.check(err));
}

TEST(KrakenConfig, SpotHasNoTestnet)
{
  std::string err;
  Endpoint e;
  e.product = Product::Spot;
  e.rest_host = kSpotRestHost;
  e.ws_host = kSpotWsAuthHost;
  EXPECT_FALSE(e.check(err));
  EXPECT_NE(err.find("no spot testnet"), std::string::npos);
  e.testnet = false;
  EXPECT_TRUE(e.check(err)) << err; // production: allowed by config, for the owner only
}

TEST(KrakenConfig, DemoHostWithTestnetFalseIsRefused)
{
  std::string err;
  Endpoint e;
  e.testnet = false;
  EXPECT_FALSE(e.check(err));
}

TEST(KrakenConfig, ExampleIniLoadsAndGuardsApply)
{
  Settings s;
  std::string err;
  ASSERT_TRUE(load_settings("config/kraken_futures_demo_example.ini", s, err)) << err;
  EXPECT_EQ(s.ep.product, Product::Futures);
  EXPECT_TRUE(s.ep.testnet);
  EXPECT_EQ(s.ep.rest_host, "demo-futures.kraken.com");
  EXPECT_EQ(s.om.symbols.at("KRAKENF_PFXBTUSD"), "PF_XBTUSD");
  EXPECT_DOUBLE_EQ(s.om.qty_unit, 1e-4);

  const std::string p = "build/kraken_test_bad.ini";
  std::ofstream(p) << "[kraken]\nproduct = futures\ntestnet = true\nrest_host = futures.kraken.com\nkey_file = /x\n";
  EXPECT_FALSE(load_settings(p, s, err));
  std::ofstream(p) << "[kraken]\nproduct = spot\ntestnet = true\nkey_file = /x\n";
  EXPECT_FALSE(load_settings(p, s, err));
  std::ofstream(p) << "[kraken]\nproduct = futures\ntestnet = true\n"; // no key file
  EXPECT_FALSE(load_settings(p, s, err));
  std::ofstream(p) << "[kraken]\nsim = true\nsymbols = A:B,C\n";
  EXPECT_FALSE(load_settings(p, s, err));
  std::remove(p.c_str());
}
