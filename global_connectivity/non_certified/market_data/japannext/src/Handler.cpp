/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "jnx_md/Handler.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

#include "jnx_md/gen/moldudp64.hpp"

namespace jnx_md
{
  namespace I = jnx::itch::from_jnx;
  namespace G = jnx::glimpse::from_jnx;
  namespace Soup = jnx::soup::from_jnx;
  using Header = jnx::moldudp64::from_jnx::DownstreamPacketHeader;

  Handler::Handler(const Config &cfg, Sink &sink, Hooks hooks)
      : cfg_(cfg), hooks_(std::move(hooks)), inst_(cfg.instruments),
        disp_(cfg.dispatch, inst_, sink, st_)
  {
  }

  void Handler::on_packet(const char *data, size_t len, uint64_t recv_ns)
  {
    ++st_.packets;
    now_ns_ = recv_ns;
    if (len < sizeof(Header))
    {
      ++st_.short_packets;
      return;
    }
    Header h;
    std::memcpy(&h, data, sizeof(h));
    const std::string sess(h.Session, sizeof(h.Session));   // all 10 bytes (section 10)
    const uint64_t seq = h.SequenceNumber;
    const uint16_t count = h.MessageCount;

    if (!have_session_)
    {
      have_session_ = true;
      session_ = sess;
      join(seq);
    }
    else if (sess != session_)
      new_session(sess, seq);

    if (mode_ == Mode::Ended)
    {
      ++st_.packets_ignored_after_eos;
      return;
    }

    if (count == 0xFFFF)
    {
      // End of Session (MOLD 3.4): the books of this session are done.
      ++st_.end_of_session;
      std::cerr << "jnx: End of Session " << session_ << " at " << seq << std::endl;
      disp_.reset();
      buffer_.clear();
      mode_ = Mode::Ended;
      return;
    }

    if (count == 0)
    {
      ++st_.heartbeats;
      if (mode_ == Mode::Live && next_ && seq > next_)
        hole(seq);   // a heartbeat carries the next number (MOLD 3.3)
      tick(recv_ns);
      return;
    }

    ++st_.data_packets;
    if (mode_ == Mode::Snapshot)
    {
      buffer(data, len, seq, count);
      return;
    }
    if (seq + count <= next_)
    {
      ++st_.dup_packets;
      return;
    }
    if (seq > next_)
    {
      buffer(data, len, seq, count);
      if (mode_ == Mode::Live)
        hole(seq);
      tick(recv_ns);
      return;
    }
    apply(data, len, seq, count);
    drain();
    tick(recv_ns);
  }

  void Handler::join(uint64_t seq)
  {
    if (hooks_.session_midnight)
      disp_.set_midnight(hooks_.session_midnight());
    switch (cfg_.join)
    {
    case Join::FirstPacket:
      next_ = seq;
      mode_ = Mode::Live;
      break;
    case Join::FromOne:
      next_ = 1;
      mode_ = Mode::Live;
      break;
    case Join::Snapshot:
      next_ = 0;
      start_snapshot();
      break;
    }
  }

  void Handler::new_session(const std::string &s, uint64_t seq)
  {
    // MOLD 2.6.3 says abort on a session change; a new session is a new trading
    // day, so the channel is reset and the new session joined (section 5).
    ++st_.session_changes;
    std::cerr << "jnx: session " << session_ << " -> " << s << std::endl;
    disp_.reset();
    buffer_.clear();
    session_ = s;
    mode_ = Mode::Live;
    join(seq);
  }

  void Handler::buffer(const char *data, size_t len, uint64_t first, uint16_t count)
  {
    if (buffer_.count(first))
    {
      ++st_.dup_packets;
      return;
    }
    ++st_.buffered_packets;
    Packet &p = buffer_[first];
    p.bytes.assign(data, len);
    p.first = first;
    p.count = count;
  }

  void Handler::apply(const char *data, size_t len, uint64_t first, uint16_t count)
  {
    disp_.packet(first);
    size_t off = sizeof(Header);
    for (uint32_t k = 0; k < count; ++k)
    {
      const uint64_t seq = first + k;
      if (off + 2 > len)
      {
        ++st_.malformed;   // fewer blocks than the header says
        break;
      }
      const auto d = I::decode_mold(data + off, len - off);
      if (d.size == 0)
      {
        ++st_.malformed;
        break;
      }
      off += d.size;
      if (seq < next_)
      {
        ++st_.old_messages;
        continue;
      }
      next_ = seq + 1;
      ++st_.messages;
      if (d)
        disp_.on_itch(d.msg, seq);
      else if (d.size == 2)
        ++st_.empty_blocks;   // a block may be empty (MOLD 3.2)
      else if (d.st == jnx::status::unknown)
        ++st_.unknown;
      else
        ++st_.malformed;
    }
    // A packet that ended early still accounts for its numbers.
    next_ = std::max(next_, first + count);
    disp_.end_of_packet(first);
  }

  void Handler::drain()
  {
    while (!buffer_.empty())
    {
      auto it = buffer_.begin();
      Packet &p = it->second;
      if (p.first + p.count <= next_)
      {
        buffer_.erase(it);   // already applied, or below a snapshot's number
        continue;
      }
      if (p.first > next_)
        break;               // a hole remains
      apply(p.bytes.data(), p.bytes.size(), p.first, p.count);
      buffer_.erase(it);
    }
    if (mode_ == Mode::Buffering)
    {
      if (buffer_.empty() && next_ >= hole_upto_)
        mode_ = Mode::Live;
      else if (next_ != requested_from_)
        rerequest();   // the reply held only part of the range (MOLD 4)
    }
  }

  void Handler::hole(uint64_t upto)
  {
    ++st_.gaps;
    st_.gap_messages += upto - next_;
    hole_upto_ = upto;
    mode_ = Mode::Buffering;
    gap_since_ns_ = now_ns_;
    rerequest();
  }

  void Handler::rerequest()
  {
    uint64_t upto = buffer_.empty() ? hole_upto_ : buffer_.begin()->first;
    if (upto <= next_)
      upto = next_ + 1;
    const uint32_t n = uint32_t(std::min<uint64_t>(upto - next_, cfg_.rerequest_max));
    requested_from_ = next_;
    ++st_.rerequests;
    if (!hooks_.rerequest || !hooks_.rerequest(session_, next_, n))
      escalate("no re-request server");
  }

  void Handler::tick(uint64_t now_ns)
  {
    now_ns_ = std::max(now_ns_, now_ns);
    if (mode_ == Mode::Buffering && now_ns_ > gap_since_ns_ + cfg_.rerequest_timeout_ns)
      escalate("re-request timed out");
  }

  void Handler::escalate(const char *why)
  {
    if (mode_ == Mode::Snapshot)
      return;
    ++st_.escalations;
    std::cerr << "jnx: gap from " << next_ << " not closed (" << why
              << "): GLIMPSE snapshot" << std::endl;
    disp_.gap();
    start_snapshot();
  }

  void Handler::force_recovery(const char *why)
  {
    std::cerr << "jnx: forced recovery (" << why << ")" << std::endl;
    start_snapshot();
  }

  void Handler::start_snapshot()
  {
    ++st_.snapshots_started;
    mode_ = Mode::Snapshot;
    disp_.reset();
    disp_.begin_snapshot();
    soup_.clear();
    if (hooks_.request_snapshot)
      hooks_.request_snapshot();
  }

  void Handler::end_snapshot(uint64_t seq)
  {
    ++st_.snapshots_done;
    disp_.end_snapshot();
    // GLIMPSE 5.8: process the real-time feed from this number.
    next_ = seq;
    mode_ = Mode::Live;
    std::cerr << "jnx: End of Snapshot, next sequence " << seq << ", " << buffer_.size()
              << " packets buffered" << std::endl;
    drain();
    if (mode_ == Mode::Live && !buffer_.empty())
      hole(buffer_.begin()->first);   // the buffer starts above the snapshot
  }

  void Handler::glimpse_failed(const char *why)
  {
    if (mode_ != Mode::Snapshot)
      return;
    std::cerr << "jnx: GLIMPSE failed (" << why << "); asking again" << std::endl;
    start_snapshot();
  }

  void Handler::on_glimpse_bytes(const char *data, size_t len)
  {
    soup_.append(data, len);
    size_t off = 0;
    while (off < soup_.size())
    {
      const auto d = Soup::decode(soup_.data() + off, soup_.size() - off);
      if (d.st == jnx::status::incomplete)
        break;
      if (d.size == 0)
      {
        // Cannot follow the stream any more.
        soup_.clear();
        glimpse_failed("undecodable SoupBinTCP packet");
        return;
      }
      off += d.size;
      if (!d)
        continue;
      if (std::holds_alternative<Soup::SequencedDataPacket>(d.msg))
        glimpse_message(d.tail, d.tail_len);
      else if (std::holds_alternative<Soup::LoginRejectedPacket>(d.msg))
        glimpse_failed("login rejected");
      else if (std::holds_alternative<Soup::EndOfSessionPacket>(d.msg))
        glimpse_failed("end of session before End of Snapshot");
      if (mode_ != Mode::Snapshot && !preloading_)
      {
        soup_.clear();   // End of Snapshot: the rest is not ours
        return;
      }
    }
    soup_.erase(0, off);
  }

  void Handler::glimpse_message(const char *p, size_t n)
  {
    if (mode_ != Mode::Snapshot && !preloading_)
      return;
    const auto d = G::decode(p, n);
    if (!d)
    {
      ++(d.st == jnx::status::unknown ? st_.unknown : st_.malformed);
      return;
    }
    ++st_.snapshot_messages;
    if (const auto *g = std::get_if<G::EndOfSnapshotMessage>(&d.msg))
    {
      ++st_.snapshot_by_type['G'];
      if (preloading_)
        return;
      end_snapshot(g->SequenceNumber);
      return;
    }
    disp_.on_glimpse(d.msg);
  }

  void Handler::preload_directory(const char *data, size_t len)
  {
    preloading_ = true;
    disp_.begin_snapshot();
    soup_.clear();
    on_glimpse_bytes(data, len);
    soup_.clear();
    preloading_ = false;
    disp_.end_snapshot(false);
    disp_.forget_time();
  }
}
