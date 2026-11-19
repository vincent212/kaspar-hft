// Instrument loader: Aquis's public SecurityList.csv format (a sample of the
// file of 2026-11-18 in tests/data), the universe file, and definitions from
// the feed.
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

#include "aquis/Instruments.hpp"

namespace
{
std::string data(const char *f) { return std::string(AQUIS_TEST_DATA) + "/" + f; }

std::string write_tmp(const std::string &name, const std::string &text)
{
  std::string p = std::string(::testing::TempDir()) + name;
  std::ofstream(p) << text;
  return p;
}
} // namespace

TEST(Instruments, SplitCsv)
{
  auto c = aquis::split_csv("\"Smith, Jones \"\"A\"\" plc\",12,XLON\r");
  ASSERT_EQ(c.size(), 3u);
  EXPECT_EQ(c[0], "Smith, Jones \"A\" plc");
  EXPECT_EQ(c[1], "12");
  EXPECT_EQ(c[2], "XLON");
}

TEST(Instruments, PublicSecurityListSample)
{
  aquis::Instruments ins;
  ins.add_universe("VODl", 1001);
  ins.add_universe("ARBBq", 1002);
  std::string err;
  ASSERT_EQ(ins.load_security_file(data("SecurityList_sample.csv"), "AQXE", &err), 7) << err;
  EXPECT_EQ(ins.size(), 7u);
  EXPECT_EQ(ins.routed(), 2u);
  // One ISIN, two listings with their own securityIDs; only the UMTF in the
  // universe is routed.
  EXPECT_EQ(ins.sec_id(2003), 1001);
  EXPECT_EQ(ins.sec_id(4421), -1);
  EXPECT_EQ(ins.find(4421)->isin, ins.find(2003)->isin);
  EXPECT_EQ(ins.sec_id(8206), 1002);
  const auto *a = ins.find(8206);
  EXPECT_EQ(a->mic, "AQSE");
  EXPECT_EQ(a->currency, "GBX");
  EXPECT_EQ(a->tick_table, "ADNT_0_10");
  EXPECT_EQ(ins.find(340)->umtf, "1U1d");
  // Rows of another operating MIC are skipped.
  aquis::Instruments eu;
  EXPECT_EQ(eu.load_security_file(data("SecurityList_sample.csv"), "AQEU", &err), 0);
}

TEST(Instruments, UniverseFileAndErrors)
{
  aquis::Instruments ins;
  std::string err;
  auto u = write_tmp("aquis_universe.csv", "# umtf,sec_id\nVODl, 1001\n\n1U1d,7\n");
  EXPECT_EQ(ins.load_universe(u, &err), 2) << err;
  ASSERT_EQ(ins.load_security_file(data("SecurityList_sample.csv"), "", &err), 7);
  EXPECT_EQ(ins.sec_id(340), 7);
  EXPECT_EQ(ins.load_universe("/nonexistent/x.csv", &err), -1);
  EXPECT_NE(err.find("cannot open"), std::string::npos);
  auto bad = write_tmp("aquis_bad.csv", "security_id,mic\n70000,XLON\n");
  EXPECT_EQ(ins.load_security_file(bad, "", &err), -1);
  auto big = write_tmp("aquis_big.csv", "security_id,mic,isin,umtf,currency\n70000,XLON,A,B,GBX\n");
  EXPECT_EQ(ins.load_security_file(big, "", &err), -1);
  EXPECT_NE(err.find("u16"), std::string::npos);
}

TEST(Instruments, FeedDefinitionReplacesFile)
{
  aquis::Instruments ins;
  ins.add_universe("NEWl", 5);
  std::string err;
  ASSERT_EQ(ins.load_security_file(data("SecurityList_sample.csv"), "", &err), 7);
  EXPECT_EQ(ins.sec_id(340), -1);
  ins.on_definition(340, "NEWl", "GB00NEW00001", "GBX", "XLON", 3, 0x11, "NEW");
  EXPECT_EQ(ins.sec_id(340), 5);
  EXPECT_EQ(ins.find(340)->flags, 0x11);
  EXPECT_FALSE(ins.tick(340, 100).has_value());
}

// The whole public file, when AQUIS_SECURITY_LIST names a copy of it.
TEST(Instruments, WholePublicFileIfGiven)
{
  const char *f = std::getenv("AQUIS_SECURITY_LIST");
  if (!f)
    GTEST_SKIP() << "AQUIS_SECURITY_LIST not set";
  aquis::Instruments ins;
  std::string err;
  const int n = ins.load_security_file(f, "", &err);
  ASSERT_GT(n, 0) << err;
  EXPECT_EQ(size_t(n), ins.size()) << "a securityID appears twice";
  std::printf("%s: %d securities, %zu distinct securityIDs\n", f, n, ins.size());
}
