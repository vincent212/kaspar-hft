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

// Starts a GLIMPSE spin. The spin's bytes come back through
// NasdaqFeed::on_glimpse_bytes. Returns false if no spin can be taken.
struct SnapshotSource
{
  virtual ~SnapshotSource() = default;
  virtual bool start() = 0;
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

  // Bytes of the GLIMPSE SoupBinTCP session, as read.
  void on_glimpse_bytes(const char *p, size_t n) { soup_.feed(p, n); }

  // Take a snapshot now (a late join, an operator command).
  bool recover()
  {
    seq_.begin_snapshot();
    if (start_snapshot())
      return true;
    seq_.cancel_snapshot();
    return false;
  }

  Dispatcher &dispatcher() { return disp_; }
  const MoldSequencer &sequencer() const { return seq_; }
  uint64_t snapshot_n() const { return last_n_; }

private:
  Dispatcher disp_;
  MoldSequencer seq_;
  Requester *req_;
  SnapshotSource *snap_;
  SoupStream soup_;
  uint64_t now_ = 0, last_n_ = 0;
  bool spinning_ = false;

  bool start_snapshot()
  {
    if (!snap_ || spinning_)
      return spinning_;
    disp_.begin_recovery();
    spinning_ = true;
    if (!snap_->start())
    {
      spinning_ = false;
      return false;
    }
    return true;
  }

  void on_spin_message(const char *p, size_t n)
  {
    if (!spinning_)
      return;
    const uint64_t g = disp_.on_glimpse(p, n);
    if (g)
    {
      spinning_ = false;
      last_n_ = g;
      disp_.end_recovery(g);
      seq_.resume_from(g, now_);
    }
  }

  // MoldSequencer::Owner
  void on_packet_begin(uint64_t seq, uint64_t recv_ns) override { disp_.begin_packet(seq, recv_ns); }
  void on_message(const char *b, size_t len, uint64_t seq) override { disp_.on_itch(b, len, seq); }
  void on_packet_end() override { disp_.end_packet(); }
  void on_gap(uint64_t, uint64_t) override { disp_.gap(); }
  void on_session(const std::string &, const std::string &) override { disp_.reset(false); }
  void request(const std::string &s, uint64_t seq, uint16_t count) override
  {
    if (req_)
      req_->request(s, seq, count);
  }
  bool need_snapshot() override { return start_snapshot(); }
};
} // namespace nasdaq_md
