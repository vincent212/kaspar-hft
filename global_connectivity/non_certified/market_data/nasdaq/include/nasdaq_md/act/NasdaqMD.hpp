#pragma once
// NasdaqMD: one market's TotalView-ITCH 5.0 feed (Nasdaq, BX or PSX) as a
// Kaspar actor. Not certified; never run against the venue.
//
//   SocketReader<uint64_t,10> A ─┐  ProcessQ<uint64_t> (mcast_recv, unchanged)
//   SocketReader<uint64_t,10> B ─┼─> NasdaqMD ── NasdaqFeed (MoldSequencer, Dispatcher)
//   re-request reader thread  ───┘      │           └─> frame::mda::msg::Data to each
//   GLIMPSE client thread ── GlimpseBytes                instrument's OB, and binrec
//
// Feeds A and B both send to this actor; the sequencer drops whatever has
// already been applied, which is the A/B arbitration. Retransmission replies
// arrive on the re-request socket, read by a thread of this actor and posted
// back as ProcessQ. A GLIMPSE spin is read by a second thread and posted as
// GlimpseBytes. A 10 ms timer drives re-request retries.
//
// Books: a vector indexed by Kaspar asset id, as handler_if's
// mbo_order_books; OBs for Nasdaq assets must be built with a ring ladder
// (ladder_ticks), since the tick is 0.0001 and maxpx 2e9.
#include "actors/Actor.hpp"
#include "actors/Message.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Timeout.hpp"
#include "mcast_recv/msg/ProcessQ.hpp"
#include "nasdaq_md/Config.hpp"
#include "nasdaq_md/NasdaqFeed.hpp"
#include "nasdaq_md/Transport.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace nasdaq_md
{
namespace msg
{
struct GlimpseBytes : public actors::MessageT<GlimpseBytes>
{
  std::string bytes;
  bool failed = false;   // the session ended without End of Snapshot
};
} // namespace msg

namespace act
{
class NasdaqMD : public actors::Actor, private Sink, private Requester, private SnapshotSource
{
public:
  NasdaqMD(const FeedConfig &cfg, std::vector<actors::Actor *> books_by_asset,
           actors::Actor *binrec);
  ~NasdaqMD() override;

  const char *get_name() const override { return name_; }
  NasdaqFeed &feed() { return *feed_; }
  Instruments &instruments() { return ins_; }

private:
  char name_[64];
  FeedConfig cfg_;
  std::vector<actors::Actor *> books_;
  actors::Actor *binrec_;
  Instruments ins_;
  std::unique_ptr<NasdaqFeed> feed_;
  std::unique_ptr<UdpRequester> req_;
  std::unique_ptr<GlimpseClient> glimpse_;
  std::thread req_thread_, glimpse_thread_;
  std::atomic<bool> running_{false};

  void start_handler(const actors::msg::Start *) noexcept;
  void shutdown_handler(const actors::msg::Shutdown *) noexcept;
  void packet_handler(const mcast_recv::msg::ProcessQ<uint64_t> *m) noexcept;
  void glimpse_handler(const msg::GlimpseBytes *m) noexcept;
  void timeout_handler(const actors::msg::Timeout *) noexcept;

  // Sink
  void to_book(int32_t asset, const bfile::l3_t &r) override;
  void to_recorder(const bfile::l3_t &r) override;
  // Requester
  void request(const std::string &session, uint64_t seq, uint16_t count) override;
  // SnapshotSource
  bool start() override;
};
} // namespace act
} // namespace nasdaq_md
