/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The helpers: decimal text kept through parsing, fixed point, the checksum
// token and CRC32 on the documentation's own strings.

#include <gtest/gtest.h>

#include "kraken_md/KrakenJson.hpp"

using namespace kraken;

TEST(KrakenJson, ParseRawKeepsTheTextOfDecimals)
{
  auto j = parse_raw(R"({"price":63197.0,"qty":0.00006120,"checksum":3310070434,"neg":-1.50,"s":"x"})");
  EXPECT_EQ(j["price"].get<std::string>(), "63197.0");
  EXPECT_EQ(j["qty"].get<std::string>(), "0.00006120");
  EXPECT_TRUE(j["checksum"].is_number_unsigned());
  EXPECT_EQ(j["checksum"].get<uint32_t>(), 3310070434u);
  EXPECT_EQ(j["neg"].get<std::string>(), "-1.50");
  EXPECT_EQ(num_text(j["price"]), "63197.0");
  EXPECT_EQ(num_text(nlohmann::json(42)), "42");
}

TEST(KrakenJson, ParseRawNestedArraysAndObjects)
{
  auto j = parse_raw(R"({"data":[{"bids":[{"price":1.5,"qty":2.0}],"asks":[]},{"x":[1,2.25,[3.0]]}]})");
  EXPECT_EQ(j["data"][0]["bids"][0]["qty"].get<std::string>(), "2.0");
  EXPECT_TRUE(j["data"][0]["asks"].is_array());
  EXPECT_EQ(j["data"][1]["x"][1].get<std::string>(), "2.25");
  EXPECT_EQ(j["data"][1]["x"][2][0].get<std::string>(), "3.0");
  EXPECT_THROW(parse_raw("{\"a\":"), nlohmann::json::parse_error);
}

TEST(KrakenJson, FixedPointIsExact)
{
  int64_t v;
  ASSERT_TRUE(to_fixed("63206.1", v));
  EXPECT_EQ(v, 632061000000000LL);
  ASSERT_TRUE(to_fixed("0.00006120", v));
  EXPECT_EQ(v, 612000LL);
  ASSERT_TRUE(to_fixed("63207.60000", v)); // REST's text of the same price
  EXPECT_EQ(v, 632076000000000LL);
  ASSERT_TRUE(to_fixed("12", v));
  EXPECT_EQ(v, 120000000000LL);
  ASSERT_TRUE(to_fixed("0.12345678900", v)); // trailing zero past 10 places
  EXPECT_FALSE(to_fixed("0.123456789012", v)); // too precise
  EXPECT_FALSE(to_fixed("1e5", v));
  EXPECT_FALSE(to_fixed("", v));
  EXPECT_FALSE(to_fixed("1.2.3", v));
}

TEST(KrakenJson, ChecksumTokenFromTheBookGuide)
{
  // book-checksum-v2, "Algorithm".
  EXPECT_EQ(checksum_token("45285.2"), "452852");
  EXPECT_EQ(checksum_token("0.00100000"), "100000");
  EXPECT_EQ(checksum_token("0.0"), "");
}

// The concatenated strings and results given by the two checksum guides.
TEST(KrakenJson, Crc32OfTheDocumentedStrings)
{
  EXPECT_EQ(crc32("452852100000452864154571953452866154571109452896154560911452902158906604529181545534914529474454749452961353800004529759945542452995187728274528351000000045283415458201545282110000000452810100000004528031545925864527907990000452776331010345277530000000452773154602737452766154452"
                  "38"),
            3310070434u);
  EXPECT_EQ(crc32("4493954523083934493951112614493951000004493951000000449500103349264495306453744955025000044959635630000449596356300004496013380724496028896757544967031439228344978567789604497923563000044939488968699449394452100004493941000000044939414296323449394250000004493941029298844939433880000449394128140860449371334687744934735630000449302227342994493021000000449302555000044930270000000449302150000004492801052404491963387000044919576100004491203563000044909766900004490198898"
                  "2"),
            1063832831u);
}

TEST(KrakenJson, Rfc3339ToNanoseconds)
{
  EXPECT_EQ(parse_rfc3339("1970-01-01T00:00:01.5Z"), 1'500'000'000ull);
  EXPECT_EQ(parse_rfc3339("2024-01-08T12:26:39.526146327Z") % 1'000'000'000ull, 526146327ull);
  EXPECT_EQ(parse_rfc3339("garbage"), 0ull);
}
