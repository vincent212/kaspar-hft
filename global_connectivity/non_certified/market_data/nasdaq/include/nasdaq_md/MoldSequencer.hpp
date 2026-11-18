#pragma once
// MoldUDP64 sequencer (feed.md sections 2, 3 and 5; Mold pp.3-6).
//
// Packets in, messages out in sequence order. One MoldUDP64 session is one
// sequence; the header numbers the packet's first message and the others
// follow implicitly. Packets from feeds A and B, and retransmissions, all come
// through on_packet: whatever has already been applied is a duplicate and is
// dropped, so A/B arbitration is the gap rule itself.
//
//   next expected = header sequence + message count of the last packet applied
//   lower          -> duplicate (or the part of it already applied)
//   higher         -> gap: buffer the packet, GAP record, re-request the hole
//   heartbeat (0) and end of session (0xFFFF) carry the next expected number
//
// A hole that re-requests do not fill within max_tries, a hole larger than
// max_hole, or more than max_buffered packets held, asks the owner for a
// GLIMPSE snapshot (need_snapshot); without one, the hole is skipped and
// counted as lost. While a snapshot is pending every packet is buffered;
// resume_from(N) then applies from N (decision 1) and re-requests anything
// between N and the first buffered packet ("snapshot too old", section 5).
//
// A packet of another session is a new session: a channel reset (the owner's
// on_session), and the sequence starts again at that packet's number (review.md,
// low impact). With snapshot_on_late_join, a session (the first, or a new one)
// whose first packet is numbered above 1 is joined late: the owner is asked for
// a snapshot.
#include "moldudp64.hpp"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace nasdaq_md
{
class MoldSequencer
{
public:
  struct Owner
  {
    virtual ~Owner() = default;
    virtual void on_packet_begin(uint64_t seq, uint64_t recv_ns) = 0;
    // One message block: the 2-byte length and the message.
    virtual void on_message(const char *block, size_t len, uint64_t seq) = 0;
    virtual void on_packet_end() = 0;
    virtual void on_gap(uint64_t from, uint64_t to) = 0;   // first detection of a hole
    virtual void on_session(const std::string &old_session, const std::string &new_session) = 0;
    virtual void request(const std::string &session, uint64_t seq, uint16_t count) = 0;
    // Return true if a snapshot will be taken (then call resume_from).
    virtual bool need_snapshot() { return false; }
  };

  struct Config
  {
    uint64_t retry_ns = 50'000'000;     // re-request again after this long without progress
    int max_tries = 5;
    uint64_t max_hole = 100'000;        // larger holes go to a snapshot
    size_t max_buffered = 100'000;      // packets held while a hole is open
    uint16_t max_request = 1000;        // messages per Request Packet
    bool snapshot_on_late_join = false; // first packet above 1: snapshot rather than start there
  };

  struct Stats
  {
    uint64_t packets = 0, heartbeats = 0, end_of_session = 0, malformed = 0;
    uint64_t dup_packets = 0, dup_messages = 0, partial_overlap = 0;
    uint64_t gaps = 0, requests = 0, requested_messages = 0;
    uint64_t lost_messages = 0, snapshots = 0, snapshot_discarded = 0, sessions = 0;
    uint64_t applied_messages = 0, buffered_peak = 0;
  };

  MoldSequencer(Owner &o, const Config &c) : own_(o), cfg_(c) {}

  void on_packet(const char *p, size_t len, uint64_t recv_ns, uint64_t now_ns)
  {
    ++st_.packets;
    if (len < sizeof(Hdr))
    {
      ++st_.malformed;
      return;
    }
    Hdr h;
    std::memcpy(&h, p, sizeof h);
    const std::string session(h.Session, sizeof h.Session);
    const uint64_t seq = h.SequenceNumber;
    const uint16_t count = h.MessageCount;

    if (session_.empty() || session != session_)
    {
      const std::string old = session_;
      session_ = session;
      buf_.clear();
      in_hole_ = false;
      if (!old.empty())
        snapshot_ = false;   // a snapshot asked for before the first packet stands
      high_ = 0;
      if (!old.empty())
      {
        ++st_.sessions;
        own_.on_session(old, session);
      }
      next_ = seq;
      if (!snapshot_ && cfg_.snapshot_on_late_join && seq > 1)
      {
        snapshot_ = true;   // set first: the spin may complete inside need_snapshot
        ++st_.snapshots;
        if (!own_.need_snapshot())
        {
          snapshot_ = false;
          --st_.snapshots;
        }
      }
    }
    if (count == 0 || count == 0xFFFF)
    {
      if (count == 0)
        ++st_.heartbeats;
      else
        ++st_.end_of_session;
      if (seq > high_)
        high_ = seq;
      if (!snapshot_ && seq > next_)
        open_hole(now_ns);
      return;
    }
    if (seq + count > high_)
      high_ = seq + count;
    if (snapshot_)
    {
      hold(seq, p, len);
      return;
    }
    if (seq + count <= next_)
    {
      ++st_.dup_packets;
      st_.dup_messages += count;
      return;
    }
    if (seq > next_)
    {
      hold(seq, p, len);
      open_hole(now_ns);
      return;
    }
    apply(p, len, recv_ns);
    drain(recv_ns, now_ns);
  }

  // Call periodically (e.g. every 10 ms).
  void on_timer(uint64_t now_ns)
  {
    if (!in_hole_ || snapshot_)
      return;
    if (now_ns - last_req_ns_ < cfg_.retry_ns)
      return;
    if (tries_ >= cfg_.max_tries)
    {
      give_up(now_ns);
      return;
    }
    send_request(now_ns);
  }

  // The snapshot holds the effect of messages below n (decision 1).
  void resume_from(uint64_t n, uint64_t now_ns)
  {
    snapshot_ = false;
    next_ = n;
    while (!buf_.empty())
    {
      auto it = buf_.begin();
      Hdr h;
      std::memcpy(&h, it->second.data(), sizeof h);
      const uint64_t end = uint64_t(h.SequenceNumber) + uint16_t(h.MessageCount);
      if (end > n)
        break;
      st_.snapshot_discarded += uint16_t(h.MessageCount);
      buf_.erase(it);
    }
    in_hole_ = false;
    drain(0, now_ns);
  }

  // Buffer everything until resume_from (a snapshot requested by the owner).
  void begin_snapshot()
  {
    snapshot_ = true;
    in_hole_ = false;
    ++st_.snapshots;
  }
  void cancel_snapshot() { snapshot_ = false; }

  uint64_t next_expected() const { return next_; }
  bool in_hole() const { return in_hole_; }
  bool awaiting_snapshot() const { return snapshot_; }
  const std::string &session() const { return session_; }
  size_t buffered() const { return buf_.size(); }
  const Stats &stats() const { return st_; }

private:
  using Hdr = nasdaq::mold::from_nasdaq::DownstreamPacketHeader;
  Owner &own_;
  Config cfg_;
  Stats st_;
  std::string session_;
  uint64_t next_ = 0, high_ = 0;
  std::map<uint64_t, std::string> buf_;   // by first sequence number
  bool in_hole_ = false, snapshot_ = false;
  uint64_t last_req_ns_ = 0;
  int tries_ = 0;

  void hold(uint64_t seq, const char *p, size_t len)
  {
    auto it = buf_.find(seq);
    if (it == buf_.end() || it->second.size() < len)
      buf_[seq].assign(p, len);
    if (buf_.size() > st_.buffered_peak)
      st_.buffered_peak = buf_.size();
  }

  // The first sequence number not held: next_ unless a hole is open.
  uint64_t hole_end() const
  {
    if (buf_.empty())
      return high_;
    return buf_.begin()->first;
  }

  void open_hole(uint64_t now_ns)
  {
    if (in_hole_)
    {
      if (buf_.size() > cfg_.max_buffered)
        give_up(now_ns, true);
      return;
    }
    in_hole_ = true;
    ++st_.gaps;
    own_.on_gap(next_, hole_end() - 1);
    if (hole_end() - next_ > cfg_.max_hole)
    {
      give_up(now_ns, true);
      return;
    }
    tries_ = 0;
    send_request(now_ns);
  }

  void send_request(uint64_t now_ns)
  {
    const uint64_t want = hole_end() - next_;
    const uint16_t n = uint16_t(want > cfg_.max_request ? cfg_.max_request : want);
    if (n == 0)
      return;
    ++tries_;
    ++st_.requests;
    st_.requested_messages += n;
    last_req_ns_ = now_ns;
    own_.request(session_, next_, n);
  }

  void give_up(uint64_t now_ns, bool immediate = false)
  {
    (void)immediate;
    snapshot_ = true;   // set first: the spin may complete inside need_snapshot
    in_hole_ = false;
    ++st_.snapshots;
    if (own_.need_snapshot())
      return;
    snapshot_ = false;
    --st_.snapshots;
    in_hole_ = true;
    // No snapshot: skip the hole.
    const uint64_t to = hole_end();
    st_.lost_messages += to - next_;
    next_ = to;
    in_hole_ = false;
    drain(0, now_ns);
  }

  void apply(const char *p, size_t len, uint64_t recv_ns)
  {
    Hdr h;
    std::memcpy(&h, p, sizeof h);
    uint64_t seq = h.SequenceNumber;
    const uint16_t count = h.MessageCount;
    const char *q = p + sizeof h;
    const char *end = p + len;
    if (seq < next_)
      ++st_.partial_overlap;
    bool begun = false;
    for (uint16_t i = 0; i < count; ++i, ++seq)
    {
      if (end - q < 2)
      {
        ++st_.malformed;
        break;
      }
      const size_t blen = size_t((uint8_t(q[0]) << 8) | uint8_t(q[1])) + 2;
      if (size_t(end - q) < blen)
      {
        ++st_.malformed;
        break;
      }
      if (seq >= next_)
      {
        if (!begun)
        {
          own_.on_packet_begin(uint64_t(h.SequenceNumber), recv_ns);
          begun = true;
        }
        own_.on_message(q, blen, seq);
        ++st_.applied_messages;
        next_ = seq + 1;
      }
      else
        ++st_.dup_messages;
      q += blen;
    }
    if (begun)
      own_.on_packet_end();
  }

  void drain(uint64_t recv_ns, uint64_t now_ns)
  {
    while (!buf_.empty())
    {
      auto it = buf_.begin();
      if (it->first > next_)
        break;
      const std::string pkt = std::move(it->second);
      buf_.erase(it);
      Hdr h;
      std::memcpy(&h, pkt.data(), sizeof h);
      if (uint64_t(h.SequenceNumber) + uint16_t(h.MessageCount) <= next_)
      {
        ++st_.dup_packets;
        continue;
      }
      apply(pkt.data(), pkt.size(), recv_ns);
    }
    if (buf_.empty() && high_ <= next_)
      in_hole_ = false;
    else if (!buf_.empty() || high_ > next_)
    {
      // Still a hole (data held beyond it, or a heartbeat said there is more).
      if (!in_hole_)
        open_hole(now_ns);
      else if (buf_.empty() || buf_.begin()->first > next_)
      {
        // progress was made: ask for what is still missing
        tries_ = 0;
        if (now_ns)
          send_request(now_ns);
      }
    }
  }
};
} // namespace nasdaq_md
