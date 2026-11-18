#pragma once
// GLIMPSE 5.0 over SoupBinTCP (feed.md section 5, recovery mechanism 3).
//
// SoupStream splits a SoupBinTCP byte stream (as read from the socket: TCP may
// split or merge packets) into packets and hands the payload of each Sequenced
// Data packet to a callback. SpinWriter builds a GLIMPSE spin as a SoupBinTCP
// byte stream from an OwnBook and its Instruments: the fake GLIMPSE server of
// the tests, and the synthetic snapshot of the alignment test.
#include "glimpse50.hpp"
#include "nasdaq_md/Instruments.hpp"
#include "nasdaq_md/OwnBook.hpp"
#include "soupbintcp.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace nasdaq_md
{
class SoupStream
{
public:
  struct Events
  {
    std::function<void(const char *, size_t)> data;           // Sequenced Data payload
    std::function<void(std::string_view session, uint64_t next_seq)> login_accepted;
    std::function<void(char reason)> login_rejected;
    std::function<void()> end_of_session;
  };

  explicit SoupStream(Events e) : ev_(std::move(e)) {}

  void feed(const char *p, size_t n)
  {
    namespace soup = nasdaq::soup::from_nasdaq;
    buf_.append(p, n);
    size_t pos = 0;
    for (;;)
    {
      auto d = soup::decode(buf_.data() + pos, buf_.size() - pos);
      if (d.st == nasdaq::status::incomplete)
        break;
      if (!d)
      {
        ++errors_;
        if (d.size == 0)
        {
          buf_.clear();   // the stream cannot be followed
          return;
        }
        pos += d.size;
        continue;
      }
      std::visit(
          [&](const auto &m) {
            using M = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<M, soup::SequencedData>)
            {
              ++packets_;
              if (ev_.data)
                ev_.data(d.tail, d.tail_len);
            }
            else if constexpr (std::is_same_v<M, soup::LoginAccepted>)
            {
              uint64_t s = 0;
              for (char c : m.get_SequenceNumber())
                if (c >= '0' && c <= '9')
                  s = s * 10 + uint64_t(c - '0');
              if (ev_.login_accepted)
                ev_.login_accepted(m.get_Session(), s);
            }
            else if constexpr (std::is_same_v<M, soup::LoginRejected>)
            {
              if (ev_.login_rejected)
                ev_.login_rejected(m.RejectReasonCode);
            }
            else if constexpr (std::is_same_v<M, soup::EndOfSession>)
            {
              if (ev_.end_of_session)
                ev_.end_of_session();
            }
          },
          d.msg);
      pos += d.size;
    }
    buf_.erase(0, pos);
  }

  uint64_t packets() const { return packets_; }
  uint64_t errors() const { return errors_; }

private:
  Events ev_;
  std::string buf_;
  uint64_t packets_ = 0, errors_ = 0;
};

// The Login Request a GLIMPSE client sends: GLIMPSE requires sequence 1
// (GLIMPSE p.2).
inline std::string glimpse_login(std::string_view user, std::string_view password)
{
  nasdaq::soup::to_nasdaq::LoginRequest l{};
  l.set_Username(user);
  l.set_Password(password);
  l.set_RequestedSession("");
  l.set_RequestedSequenceNumber("1");
  char out[64];
  const size_t n = nasdaq::soup::to_nasdaq::encode(out, sizeof out, l);
  return std::string(out, n);
}

class SpinWriter
{
public:
  // Appends one GLIMPSE message as a Sequenced Data packet.
  template <class M>
  void put(const M &m)
  {
    const uint16_t len = uint16_t(sizeof(M) + 1);
    out_.push_back(char(len >> 8));
    out_.push_back(char(len & 0xff));
    out_.push_back('S');
    out_.append(reinterpret_cast<const char *>(&m), sizeof(M));
  }

  void login_accepted(std::string_view session)
  {
    nasdaq::soup::from_nasdaq::LoginAccepted a{};
    a.MessageLength = uint16_t(sizeof(a) - 2);
    a.MessageType = 'A';
    a.set_Session(session);
    a.set_SequenceNumber("1");
    out_.append(reinterpret_cast<const char *>(&a), sizeof a);
  }

  // A whole spin: Stock Directory for every known locate, Stock Trading Action
  // and Operational Halt where known, Add Order for every order (in queue order
  // within each level, decision 2), End of Snapshot n. Every message carries
  // spin_ts as its Timestamp (GLIMPSE does not say what a spun Add's Timestamp
  // is; review.md item 2).
  void spin(const OwnBook &book, const Instruments &ins, uint64_t n, uint64_t spin_ts,
            char market_code)
  {
    namespace gl = nasdaq::glimpse::from_nasdaq;
    for (uint32_t loc = 1; loc < 65536; ++loc)
    {
      const auto &i = ins.at(uint16_t(loc));
      if (!i.known)
        continue;
      gl::StockDirectory r{};
      r.MessageType = 'R';
      r.StockLocate = uint16_t(loc);
      r.Timestamp = spin_ts;
      r.set_Stock(i.symbol);
      r.MarketCategory = i.market_category;
      r.FinancialStatusIndicator = ' ';
      r.RoundLotSize = i.round_lot;
      r.RoundLotsOnly = 'N';
      r.IssueClassification = ' ';
      r.set_IssueSubType("");
      r.Authenticity = i.authenticity;
      r.ShortSaleThresholdIndicator = ' ';
      r.IPOFlag = ' ';
      r.LULDReferencePriceTier = ' ';
      r.ETPFlag = ' ';
      r.ETPLeverageFactor = 0;
      r.InverseIndicator = 'N';
      put(r);
      if (i.trading_state)
      {
        gl::StockTradingAction h{};
        h.MessageType = 'H';
        h.StockLocate = uint16_t(loc);
        h.Timestamp = spin_ts;
        h.set_Stock(i.symbol);
        h.TradingState = i.trading_state;
        h.Reserved = ' ';
        h.set_Reason("");
        put(h);
      }
      if (i.op_halt)
      {
        gl::OperationalHalt o{};
        o.MessageType = 'h';
        o.StockLocate = uint16_t(loc);
        o.Timestamp = spin_ts;
        o.set_Stock(i.symbol);
        o.MarketCode = market_code;
        o.OperationalHaltAction = 'H';
        put(o);
      }
    }
    for (const auto &[k, orders] : book.queues())
      for (const auto &[ref, o] : orders)
      {
        gl::AddOrder a{};
        a.MessageType = 'A';
        a.StockLocate = o.locate;
        a.Timestamp = spin_ts;
        a.OrderReferenceNumber = ref;
        a.BuySellIndicator = o.side;
        a.Shares = o.shares;
        a.set_Stock(ins.at(o.locate).symbol);
        a.Price = o.px;
        put(a);
      }
    gl::EndOfSnapshot g{};
    g.MessageType = 'G';
    g.set_SequenceNumber(std::to_string(n));
    put(g);
  }

  const std::string &bytes() const { return out_; }
  void clear() { out_.clear(); }

private:
  std::string out_;
};
} // namespace nasdaq_md
