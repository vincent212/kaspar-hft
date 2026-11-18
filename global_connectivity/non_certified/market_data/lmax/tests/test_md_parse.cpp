/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The generated dictionary against the messages of LMAX's Broker Market Data
// spec, the V the handler builds, and the W/Y parsers.

#include <gtest/gtest.h>

#include "quickfix/DataDictionary.h"
#include "quickfix/Message.h"

#include "FixText.hpp"
#include "lmax_fix/LmaxFix.hpp"
#include "lmax_md/MdParse.hpp"

using namespace lmax;
using namespace lmax::md;
using lmax::test::fixmsg;

namespace
{
  const FIX::DataDictionary &dict()
  {
    static FIX::DataDictionary d(LMAX_MD_DICT);
    return d;
  }

  // A W as LMAX's spec lists it: MDReqID, Instrument (48, 22), NoMDEntries
  // with 269, 270, 271, 272, 273.
  std::string w_text(int seq = 7)
  {
    return fixmsg("W",
               "262=1a|48=4001|22=8|268=5|"
               "269=0|270=1.15231|271=10|272=20261118|273=10:00:00.123|"
               "269=0|270=1.15230|271=25.5|"
               "269=1|270=1.15233|271=3|"
               "269=1|270=1.15235|271=40|"
               "269=2|270=1.15233|271=1|272=20261118|273=09:59:59.900|",
               seq);
  }
}

TEST(LmaxDictionary, ParsesEveryMessageOfTheSpec)
{
  const char *msgs[][2] = {
      {"A", "98=0|108=30|141=Y|553=user|554=pass|"},
      {"0", "112=x|"},
      {"1", "112=x|"},
      {"2", "7=1|16=0|"},
      {"3", "45=3|371=48|372=V|373=5|58=bad|"},
      {"4", "123=Y|36=10|"},
      {"5", "58=bye|"},
      {"V", "262=1a|263=1|264=20|265=0|266=Y|267=3|269=0|269=1|269=2|146=1|48=4001|22=8|"},
      {"Y", "262=1a|281=0|58=Unknown symbol|"},
  };
  for (auto &m : msgs)
  {
    FIX::Message parsed;
    ASSERT_NO_THROW(parsed = FIX::Message(fixmsg(m[0], m[1]), dict(), true)) << m[0];
    ASSERT_NO_THROW(dict().validate(parsed)) << m[0];
  }
  ASSERT_NO_THROW(dict().validate(FIX::Message(w_text(), dict(), true)));
}

TEST(LmaxDictionary, RejectsAnUnknownMessageTypeAndAMissingRequiredField)
{
  // X (incremental refresh) is not in LMAX's Broker Market Data spec.
  EXPECT_ANY_THROW(dict().validate(FIX::Message(fixmsg("X", "262=1|268=0|"), dict(), true)));
  // W requires SecurityID.
  EXPECT_ANY_THROW(dict().validate(FIX::Message(fixmsg("W", "262=1|22=8|268=0|"), dict(), true)));
  // SubscriptionRequestType 0 (snapshot only) is not an LMAX value.
  EXPECT_ANY_THROW(dict().validate(FIX::Message(
      fixmsg("V", "262=1a|263=0|264=20|267=1|269=0|146=1|48=4001|22=8|"), dict(), true)));
}

TEST(LmaxMdParse, Snapshot)
{
  FIX::Message m(w_text(9), dict(), true);
  MdSnapshot s;
  std::string err;
  ASSERT_TRUE(parse_snapshot(m, s, err)) << err;
  EXPECT_EQ(s.md_req_id, "1a");
  EXPECT_EQ(s.security_id, "4001");
  EXPECT_EQ(s.seq, 9u);
  EXPECT_EQ(s.sending_ns, fix::utc_timestamp_ns("20261118-10:00:00.000"));
  ASSERT_EQ(s.entries.size(), 5u);
  EXPECT_EQ(s.entries[0].type, '0');
  EXPECT_DOUBLE_EQ(s.entries[0].px, 1.15231);
  EXPECT_DOUBLE_EQ(s.entries[1].size, 25.5);
  EXPECT_EQ(s.entries[0].date, "20261118");
  EXPECT_EQ(s.entries[0].time, "10:00:00.123");
  EXPECT_TRUE(s.entries[1].date.empty());
  EXPECT_EQ(s.entries[4].type, '2');
}

TEST(LmaxMdParse, EmptyBook)
{
  FIX::Message m(fixmsg("W", "262=1a|48=4001|22=8|268=0|"), dict(), true);
  MdSnapshot s;
  std::string err;
  ASSERT_TRUE(parse_snapshot(m, s, err)) << err;
  EXPECT_TRUE(s.entries.empty());
}

TEST(LmaxMdParse, Reject)
{
  FIX::Message m(fixmsg("Y", "262=1a|281=0|58=Unknown symbol|"), dict(), true);
  MdReject r;
  std::string err;
  ASSERT_TRUE(parse_md_reject(m, r, err));
  EXPECT_EQ(r.md_req_id, "1a");
  EXPECT_EQ(r.reason, '0');
  EXPECT_EQ(r.text, "Unknown symbol");
}

TEST(LmaxMdParse, RequestIds)
{
  EXPECT_EQ(md_req_id(1), "1");
  EXPECT_EQ(md_req_id(255), "ff");
  EXPECT_EQ(md_req_id(~0ULL), "ffffffffffffffff");
  EXPECT_TRUE(valid_md_req_id("1a"));
  EXPECT_FALSE(valid_md_req_id("0a"));
  EXPECT_FALSE(valid_md_req_id("1A"));
  EXPECT_FALSE(valid_md_req_id("12345678901234567"));
  EXPECT_FALSE(valid_md_req_id(""));
}

TEST(LmaxMdParse, RequestAsBuiltValidatesAgainstTheDictionary)
{
  FIX::Message v = md_request("2b", "4001", 20, true);
  v.getHeader().setField(49, "CLIENT");
  v.getHeader().setField(56, "LMXBDM");
  v.getHeader().setField(34, "2");
  v.getHeader().setField(52, "20261118-10:00:00.000");
  const std::string text = v.toString();
  EXPECT_NE(lmax::test::visible(text).find("267=3|269=0|269=1|269=2|146=1|48=4001|22=8|"), std::string::npos)
      << lmax::test::visible(text);
  FIX::Message back;
  ASSERT_NO_THROW(back = FIX::Message(text, dict(), true)) << lmax::test::visible(text);
  ASSERT_NO_THROW(dict().validate(back)) << lmax::test::visible(text);
  EXPECT_NE(lmax::test::visible(text).find("|262=2b|263=1|264=20|265=0|266=Y|267=3|"), std::string::npos);
  EXPECT_EQ(back.getField(263), "1");
  EXPECT_EQ(back.getField(264), "20");
  EXPECT_EQ(back.getField(265), "0");
  EXPECT_EQ(back.getField(266), "Y");

  FIX::Message u = md_request("2b", "4001", 20, false, true);
  EXPECT_EQ(u.getField(263), "2");
  EXPECT_EQ(u.getField(267), "2");
}

TEST(LmaxFix, HostGuard)
{
  std::string why;
  EXPECT_TRUE(fix::host_allowed(fix::Env::Demo, "fix-marketdata.london-demo.lmax.com", why));
  EXPECT_TRUE(fix::host_allowed(fix::Env::Demo, "FIX-ORDER.LONDON-DEMO.LMAX.COM", why));
  EXPECT_FALSE(fix::host_allowed(fix::Env::Demo, "fix-marketdata.london-uat.lmax.com", why));
  EXPECT_FALSE(fix::host_allowed(fix::Env::Demo, "fix-market-data.london-perpetual.lmax.com", why));
  EXPECT_FALSE(fix::host_allowed(fix::Env::Demo, "fix.lmaxtrader.com", why));
  EXPECT_FALSE(fix::host_allowed(fix::Env::Demo, "91.215.166.66", why));
  EXPECT_FALSE(fix::host_allowed(fix::Env::Demo, "london-demo.lmax.com.evil.example", why));
  EXPECT_FALSE(fix::host_allowed(fix::Env::Demo, ".london-demo.lmax.com", why));
  EXPECT_FALSE(fix::host_allowed(fix::Env::Demo, "127.0.0.1", why));
  EXPECT_TRUE(fix::host_allowed(fix::Env::Uat, "fix-order.london-uat.lmax.com", why));
  EXPECT_FALSE(fix::host_allowed(fix::Env::Production, "fix-order.london-demo.lmax.com", why));
  EXPECT_FALSE(fix::host_allowed(fix::Env::Production, "fix-market-data.london-perpetual.lmax.com", why));
  EXPECT_TRUE(fix::host_allowed(fix::Env::Loopback, "127.0.0.1", why));
  EXPECT_FALSE(fix::host_allowed(fix::Env::Loopback, "fix-order.london-demo.lmax.com", why));
}

TEST(LmaxFix, SettingsRefuseAProductionHost)
{
  fix::SessionParams p;
  p.env = fix::Env::Demo;
  p.host = "fix-market-data.london-perpetual.lmax.com";
  p.sender = "s";
  p.target = "t";
  p.dictionary = LMAX_MD_DICT;
  std::string text, err;
  EXPECT_FALSE(fix::settings_text(p, text, err));
  EXPECT_NE(err.find("london-demo"), std::string::npos) << err;
  p.host = "fix-marketdata.london-demo.lmax.com";
  ASSERT_TRUE(fix::settings_text(p, text, err)) << err;
  EXPECT_NE(text.find("SocketConnectHost=fix-marketdata.london-demo.lmax.com"), std::string::npos);
  EXPECT_NE(text.find("ResetOnLogon=Y"), std::string::npos);
  EXPECT_EQ(text.find("554"), std::string::npos); // no credentials in settings
}

TEST(LmaxFix, Times)
{
  EXPECT_EQ(fix::utc_timestamp_ns("19700101-00:00:01"), 1000000000ULL);
  EXPECT_EQ(fix::utc_timestamp_ns("19700102-00:00:00.5"), 86400ULL * 1000000000ULL + 500000000ULL);
  EXPECT_EQ(fix::utc_timestamp_ns("20261118-10:00:00.123456789") % 1000000000ULL, 123456789ULL);
  EXPECT_EQ(fix::utc_timestamp_ns("2026111810:00:00"), 0u);
  EXPECT_EQ(fix::utc_date_time_ns("19700101", "00:01:00"), 60ULL * 1000000000ULL);
  int64_t n;
  EXPECT_TRUE(fix::to_steps(1.15231, 0.00001, n));
  EXPECT_EQ(n, 115231);
  EXPECT_FALSE(fix::to_steps(1.152315, 0.00001, n));
  EXPECT_TRUE(fix::to_steps(0.3, 0.1, n));
  EXPECT_EQ(n, 3);
}
