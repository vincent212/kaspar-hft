#pragma once
// One market's TotalView-ITCH feed, transport-independent: MoldUDP64 packets
// in (feeds A and B and retransmissions alike), records out through a Sink.
// The sequencer orders the messages, the dispatcher maps them, and a GLIMPSE
// spin, when the sequencer asks for one, rebuilds the books.
//
// The actor (act/NasdaqMD.hpp) and the replay tool both drive this class; the
// sockets live in the actor, the re-request and GLIMPSE transports behind the
// two interfaces below.
#include "nasdaq_md/Dispatcher.hpp"
#include "nasdaq_md/Glimpse.hpp"
#include "nasdaq_md/MoldSequencer.hpp"

#include <memory>
#include <string>

namespace nasdaq_md
{
// Sends a MoldUDP64 Request Packet to the re-request server.
struct Requester
{
  virtual ~Requester() = default;
  virtual void request(const std::string &session, uint64_t seq, uint16_t count) = 0;
};

// Starts a GLIMPSE spin for one MoldUDP64 session. The spin's bytes come back
// through NasdaqFeed::on_glimpse_bytes, and its failure through
// on_glimpse_failed, each tagged with that session: the feed drops what
// belongs to a spin it no longer waits for (the session changed, or the spin
// was given up). Returns false if no spin can be taken. cancel() is called when
// a spin in flight is no longer wanted; what it still delivers is dropped.
struct SnapshotSource
{
  virtual ~SnapshotSource() = default;
  virtual bool start(const std::string &session) = 0;
  virtual void cancel() {}
};

class NasdaqFeed : private MoldSequencer::Owner
{
public:
  NasdaqFeed(const Dispatcher::Config &dc, const MoldSequencer::Config &sc, Instruments &ins,
             Sink &sink, Requester *req = nullptr, SnapshotSource *snap = nullptr)
      : disp_(dc, ins, sink), seq_(*this, sc), req_(req), snap_(snap),
        soup_(SoupStream::Events{
            [this](const char *p, size_t n) { on_spin_message(p, n); }, {}, {}, {}})
  {
  }

  void on_packet(const char *p, size_t len, uint64_t recv_ns, uint64_t now_ns)
  {
    now_ = now_ns;
    seq_.on_packet(p, len, recv_ns, now_ns);
  }
  void on_timer(uint64_t now_ns)
  {
    now_ = now_ns;
    seq_.on_timer(now_ns);
  }

  // Bytes of the GLIMPSE SoupBinTCP session, as read, for the spin started
  // for `session` (empty: the current spin, for callers with one source).
  void on_glimpse_bytes(const char *p, size_t n, const std::string &session = {})
  {
    if (!spinning_ || (!session.empty() && session != spin_session_))
    {
      ++spin_bytes_dropped_;
      return;
    }
    soup_.feed(p, n);
  }

  // The spin started for `session` ended without End of Snapshot, or could
  // not be opened. The sequencer stops holding packets and goes on live from
  // the first it holds (the hole, if any, is lost); the books keep what the
  // partial spin gave them. The owner retries with recover(), with a backoff.
  void on_glimpse_failed(const std::string &session = {})
  {
    if (!spinning_ || (!session.empty() && session != spin_session_))
      return;
    ++spins_failed_;
    end_spin();
    seq_.abandon_snapshot(now_);
  }

  // Take a snapshot now (a late join, an operator command, a retry).
  bool recover()
  {
    if (spinning_)
      return true;
    seq_.begin_snapshot();
    if (start_snapshot())
      return true;
    seq_.cancel_snapshot();
    return false;
  }

  Dispatcher &dispatcher() { return disp_; }
  const MoldSequencer &sequencer() const { return seq_; }
  uint64_t snapshot_n() const { return last_n_; }
  bool spinning() const { return spinning_; }
  const std::string &spin_session() const { return spin_session_; }
  uint64_t spins_done() const { return spins_done_; }
  uint64_t spins_failed() const { return spins_failed_; }
  uint64_t spins_cancelled() const { return spins_cancelled_; }
  uint64_t spin_bytes_dropped() const { return spin_bytes_dropped_; }

private:
  Dispatcher disp_;
  MoldSequencer seq_;
  Requester *req_;
  SnapshotSource *snap_;
  SoupStream soup_;
  uint64_t now_ = 0, last_n_ = 0;
  bool spinning_ = false;
  std::string spin_session_;   // the MoldUDP64 session the spin in flight is for
  uint64_t spins_done_ = 0, spins_failed_ = 0, spins_cancelled_ = 0, spin_bytes_dropped_ = 0;

  bool start_snapshot()
  {
    if (!snap_ || spinning_)
      return spinning_;
    disp_.begin_recovery();
    spinning_ = true;
    spin_session_ = seq_.session();
    soup_.reset();
    if (!snap_->start(spin_session_))
    {
      spinning_ = false;
      return false;
    }
    return true;
  }

  // The spin is over, complete or not: the dispatcher leaves recovery with
  // what it got (n = 0: no boundary to check).
  void end_spin(uint64_t n = 0)
  {
    spinning_ = false;
    soup_.reset();
    disp_.end_recovery(n);
  }

  void on_spin_message(const char *p, size_t n)
  {
    if (!spinning_)
      return;
    const uint64_t g = disp_.on_glimpse(p, n);
    if (g)
    {
      ++spins_done_;
      last_n_ = g;
      end_spin(g);
      seq_.resume_from(g, now_);
    }
  }

  // MoldSequencer::Owner
  void on_packet_begin(uint64_t seq, uint64_t recv_ns) override { disp_.begin_packet(seq, recv_ns); }
  void on_message(const char *b, size_t len, uint64_t seq) override { disp_.on_itch(b, len, seq); }
  void on_packet_end() override { disp_.end_packet(); }
  void on_gap(uint64_t, uint64_t) override { disp_.gap(); }
  void on_session(const std::string &, const std::string &) override
  {
    if (spinning_)
    {
      // The spin in flight is the old session's: what it still delivers is
      // dropped (tagged with that session), and the dispatcher is reset below.
      ++spins_cancelled_;
      end_spin();
      snap_->cancel();
    }
    disp_.reset(false);
  }
  void request(const std::string &s, uint64_t seq, uint16_t count) override
  {
    if (req_)
      req_->request(s, seq, count);
  }
  bool need_snapshot() override { return start_snapshot(); }
};
} // namespace nasdaq_md
