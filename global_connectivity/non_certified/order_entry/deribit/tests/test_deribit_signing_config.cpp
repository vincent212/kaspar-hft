/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Request signing against the documentation's examples, the environment
// guard, the key file and the INI configuration.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>

#include "deribit_oe/DeribitConfig.hpp"
#include "deribit_oe/Signing.hpp"

using namespace deribit::oe;

// docs.deribit.com/articles/authentication, "Client Signature", Example:
//   ClientSecret=AMANDASECRECT Timestamp=1576074319000 Nonce=1iqt2wls Data=""
//   -> 56590594f97921b09b18f166befe0d1319b198bbcdad7ca73382de2f88fe9aa1
TEST(DeribitSigning, WebSocketSignatureMatchesTheDocumentationExample)
{
  EXPECT_EQ(ws_signature("AMANDASECRECT", 1576074319000ull, "1iqt2wls", ""),
            "56590594f97921b09b18f166befe0d1319b198bbcdad7ca73382de2f88fe9aa1");
}

TEST(DeribitSigning, AuthParamsCarryTheSignatureNotTheSecret)
{
  auto p = auth_params("AMANDA", "AMANDASECRECT", 1576074319000ull, "1iqt2wls");
  EXPECT_EQ(p["grant_type"], "client_signature");
  EXPECT_EQ(p["client_id"], "AMANDA");
  EXPECT_EQ(p["timestamp"], 1576074319000ull);
  EXPECT_EQ(p["nonce"], "1iqt2wls");
  EXPECT_EQ(p["data"], "");
  EXPECT_EQ(p["signature"], "56590594f97921b09b18f166befe0d1319b198bbcdad7ca73382de2f88fe9aa1");
  EXPECT_EQ(p.dump().find("AMANDASECRECT"), std::string::npos);
}

// docs.deribit.com/articles/authentication, "Example - HTTP REST Signature":
// the printed output 9bfbc51a...276ab is labelled "depends on current
// timestamp", but it is reproduced exactly by the WebSocket example's
// Timestamp=1576074319000 and Nonce=1iqt2wls with
//   GET /api/v2/private/get_account_summary?currency=BTC, empty body.
TEST(DeribitSigning, RestSignatureMatchesTheDocumentationExample)
{
  const std::string uri = "/api/v2/private/get_account_summary?currency=BTC";
  const std::string expect = "9bfbc51a2bc372d72cc396cf1a213dc78d42eb74cb7dc272351833ad0de276ab";
  EXPECT_EQ(rest_signature("AMANDASECRECT", 1576074319000ull, "1iqt2wls", "GET", uri, ""), expect);
  EXPECT_EQ(rest_signature("AMANDASECRECT", 1576074319000ull, "1iqt2wls", "get", uri, ""), expect);
  const auto h = rest_authorization("AMANDA", "AMANDASECRECT", 1576074319000ull, "1iqt2wls", "GET", uri, "");
  EXPECT_EQ(h, "deri-hmac-sha256 id=AMANDA,ts=1576074319000,nonce=1iqt2wls,sig=" + expect);
}

TEST(DeribitSigning, NonceIsEightLowercaseAlphanumerics)
{
  for (int i = 0; i < 100; ++i)
  {
    auto n = make_nonce();
    ASSERT_EQ(n.size(), 8u);
    for (char c : n)
      EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'));
  }
}

TEST(DeribitEnvironment, TestRequiresTheTestHost)
{
  std::string err;
  Endpoint e;
  EXPECT_TRUE(e.check(err)) << err;
  e.host = "www.deribit.com";
  EXPECT_FALSE(e.check(err));
  e.host = "deribit.com";
  EXPECT_FALSE(e.check(err));
  e.host = "test.deribit.com.evil.example";
  EXPECT_FALSE(e.check(err));
}

TEST(DeribitEnvironment, ProductionNeedsAllowProductionAndTheProductionHost)
{
  std::string err;
  Endpoint e;
  e.test = false;
  e.host = "www.deribit.com";
  EXPECT_FALSE(e.check(err));
  e.allow_production = true;
  EXPECT_TRUE(e.check(err)) << err;
  e.host = "test.deribit.com";
  EXPECT_FALSE(e.check(err));
}

TEST(DeribitKeys, KeyFileErrorsNeverContainItsContent)
{
  const std::string path = "build/deribit_test_keys.json";
  {
    std::ofstream(path) << R"({"client_id": "ID-SECRETLOOKING", "client_secret": ""})";
  }
  Keys k;
  std::string err;
  EXPECT_FALSE(load_keys(path, k, err));
  EXPECT_EQ(err.find("SECRETLOOKING"), std::string::npos);
  {
    std::ofstream(path) << R"({"client_id": "abc", "client_secret": "xyz"})";
  }
  EXPECT_TRUE(load_keys(path, k, err)) << err;
  EXPECT_EQ(k.client_id, "abc");
  std::remove(path.c_str());
  EXPECT_FALSE(load_keys("/nonexistent/keys.json", k, err));
}

TEST(DeribitConfigFile, ReadsTheExampleConfiguration)
{
  DeribitOMConfig c;
  std::string err;
  ASSERT_TRUE(DeribitOMConfig::from_file("config/deribit_test_example.ini", c, err)) << err;
  EXPECT_TRUE(c.ep.test);
  EXPECT_EQ(c.ep.host, "test.deribit.com");
  ASSERT_EQ(c.instruments.size(), 1u);
  EXPECT_EQ(c.instruments[0].name, "BTC-PERPETUAL");
  EXPECT_EQ(c.instruments[0].qty_unit, "10");
  EXPECT_TRUE(c.instruments[0].inverse);
  EXPECT_TRUE(c.valid(err)) << err;
}

TEST(DeribitConfigFile, ProductionHostInATestConfigurationIsRefused)
{
  const std::string path = "build/deribit_bad.ini";
  {
    std::ofstream(path) << "[deribit]\nenvironment = test\nhost = www.deribit.com\n"
                           "[instrument.b]\nasset = A\ninstrument = BTC-PERPETUAL\ncurrency = BTC\n"
                           "tick = 0.5\nqty_unit = 10\n";
  }
  DeribitOMConfig c;
  std::string err;
  ASSERT_TRUE(DeribitOMConfig::from_file(path, c, err)) << err;
  EXPECT_FALSE(c.valid(err));
  EXPECT_NE(err.find("test.deribit.com"), std::string::npos);
  std::remove(path.c_str());
}

TEST(DeribitDecimals, ScaledAndUnits)
{
  EXPECT_EQ(scaled(126001, "0.5"), "63000.5");
  EXPECT_EQ(scaled(25, "10"), "250");
  EXPECT_EQ(scaled(150, "0.001"), "0.150");
  bool exact = false;
  EXPECT_EQ(units_of(250, "10", exact), 25);
  EXPECT_TRUE(exact);
  EXPECT_EQ(units_of(0.15, "0.001", exact), 150);
  EXPECT_TRUE(exact);
  units_of(5, "10", exact);
  EXPECT_FALSE(exact);
}
