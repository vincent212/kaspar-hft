/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit tests for mdp3::Reconstructor -- the single serialization point of the
 * parallel MBO decode path.
 *
 * It receives per-message ParsedMsg batches (out of order, tagged order_seq),
 * resequences them into a dense in-order stream, owns the orderID->securityID
 * map, and routes each entry to the right per-symbol book (mbo_order_books).
 *
 * We drive its handlers directly and observe the frame::mda::msg::Data it sends
 * to a mock book actor.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <variant>
#include <vector>

#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"
#include "mdp3/act/Reconstructor.hpp"
#include "mdp3/msg/ParsedMsg.hpp"
#include "mdp3/msg/AssetMap.hpp"
#include "mdp3/msg/ResetMBO.hpp"
#include "frame/mda/msg/Data.hpp"
#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"

using namespace unit_test;

namespace {

// Build a ParsedMsg carrying a single MBO order (add/delete) entry.
static mdp3::msg::ParsedMsg *make_order(uint64_t order_seq, uint64_t orderID,
                                        int32_t securityID, uint8_t action /*0=New,2=Del*/)
{
  auto *pm = new mdp3::msg::ParsedMsg(order_seq);
  bfile::l3_mbo_v2_t l3;
  std::memset(&l3, 0, sizeof(l3));
  l3.typ = en::l3::MBO_V2;
  l3.orderUpdateAction = action;
  l3.orderID = orderID;
  l3.securityID = securityID;
  pm->entries.push_back(l3);
  return pm;
}

// Build a ParsedMsg carrying a single MBO trade (routes via the orderID map).
static mdp3::msg::ParsedMsg *make_trade(uint64_t order_seq, uint64_t orderID)
{
  auto *pm = new mdp3::msg::ParsedMsg(order_seq);
  bfile::l3_mbo_trd_v2_t l3;
  std::memset(&l3, 0, sizeof(l3));
  l3.typ = en::l3::MBOT_V2;
  l3.orderID = orderID;
  pm->entries.push_back(l3);
  return pm;
}

// orderID of a Data the book received (works for both MBO order and trade l3s).
static uint64_t data_order_id(const frame::mda::msg::Data *d)
{
  if (std::holds_alternative<bfile::l3_mbo_v2_t>(d->l3))
    return std::get<bfile::l3_mbo_v2_t>(d->l3).orderID;
  if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(d->l3))
    return std::get<bfile::l3_mbo_trd_v2_t>(d->l3).orderID;
  return 0;
}

class ReconstructorTest : public ::testing::Test
{
protected:
  MockActor book{"MockBook"};
  std::vector<actor_ptr> books{&book}; // asset_id 0 -> mock book
  mdp3::Reconstructor recon{books, en::x::CMEMDFUT};

  static constexpr int32_t SEC = 100; // a securityID we map to asset_id 0

  void feed(const actors::Message *m) { TestHelper::invoke_handler(&recon, m, nullptr); }

  void map_security() // AssetMap: securityID SEC -> asset_id 0 (the mock book)
  {
    mdp3::msg::AssetMap am(SEC, 0);
    feed(&am);
  }

  std::vector<uint64_t> routed_order_ids() const
  {
    std::vector<uint64_t> ids;
    for (size_t i = 0; i < book.message_count(); ++i)
      ids.push_back(data_order_id(book.get_message<frame::mda::msg::Data>(i)));
    return ids;
  }
};

// Out-of-order ParsedMsgs are applied to the book in order_seq order.
TEST_F(ReconstructorTest, ResequencesOutOfOrderIntoWireOrder)
{
  map_security();

  // order_seq 0,1,2 carry orderIDs 10,20,30 -- but arrive 2, 0, 1.
  auto *p2 = make_order(2, 30, SEC, 0);
  auto *p0 = make_order(0, 10, SEC, 0);
  auto *p1 = make_order(1, 20, SEC, 0);
  feed(p2);
  EXPECT_EQ(book.message_count(), 0u); // buffered, nothing applied yet
  feed(p0);
  EXPECT_EQ(book.message_count(), 1u); // only 0 can drain
  feed(p1);                            // 1 then buffered 2 drain
  EXPECT_EQ(routed_order_ids(), (std::vector<uint64_t>{10, 20, 30}));
  delete p0; delete p1; delete p2;
}

// An entry whose securityID has no asset mapping yet is dropped, not routed.
TEST_F(ReconstructorTest, UnmappedSecurityIsDropped)
{
  // no AssetMap sent -> SEC unknown
  auto *p0 = make_order(0, 10, SEC, 0);
  feed(p0);
  EXPECT_EQ(book.message_count(), 0u);

  map_security();                    // now map it
  auto *p1 = make_order(1, 20, SEC, 0);
  feed(p1);
  EXPECT_EQ(routed_order_ids(), (std::vector<uint64_t>{20})); // 10 was dropped
  delete p0; delete p1;
}

// A trade routes via the orderID map (populated by a prior New).
TEST_F(ReconstructorTest, TradeRoutesViaOrderIdMap)
{
  map_security();
  auto *n = make_order(0, 5, SEC, 0); // New order 5 -> orderID map[5]=SEC
  auto *t = make_trade(1, 5);         // trade on order 5
  feed(n);
  feed(t);
  EXPECT_EQ(routed_order_ids(), (std::vector<uint64_t>{5, 5})); // both routed
  delete n; delete t;
}

// ChannelReset (ResetMBO) clears the orderID map: a later trade for a reused
// orderID no longer resolves and is dropped.
TEST_F(ReconstructorTest, ResetClearsOrderIdMap)
{
  map_security();
  auto *n = make_order(0, 5, SEC, 0);
  auto *t1 = make_trade(1, 5);
  feed(n);
  feed(t1);
  EXPECT_EQ(book.message_count(), 2u); // New + trade routed

  mdp3::msg::ResetMBO reset;
  feed(&reset); // book cleared -> drop the orderID map

  auto *t2 = make_trade(2, 5); // reused orderID, no longer known
  feed(t2);
  EXPECT_EQ(book.message_count(), 2u); // t2 dropped, still 2
  delete n; delete t1; delete t2;
}

} // namespace
