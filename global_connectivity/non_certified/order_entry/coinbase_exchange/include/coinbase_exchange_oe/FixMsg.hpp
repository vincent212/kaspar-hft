#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// FixMsg: one FIX message as an ordered list of (tag, value). The order
// manager and the drop-copy client build and read Coinbase's application
// messages as FixMsg, so their logic is tested without QuickFIX; the QuickFIX
// session converts at the edge (its toString() in, a FIX::Message out).
//
// parse() reads tag=value strings with SOH (or another separator, e.g. '|'
// in tests and in the documentation), and checks BodyLength and CheckSum when
// they are present. to_wire() writes BeginString, BodyLength, the fields and
// CheckSum.

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coinbase_exchange::fix
{
  // Tags used by the Coinbase handlers (FIX 5.0 SP2 numbers; 8013, 8014,
  // 7928 and 9406 are Coinbase's own).
  enum Tag : int
  {
    BeginString = 8, BodyLength = 9, CheckSum = 10, MsgType = 35, SenderCompID = 49, TargetCompID = 56,
    MsgSeqNum = 34, SendingTime = 52, PossDupFlag = 43, PossResend = 97,
    AvgPx = 6, ClOrdID = 11, CumQty = 14, ExecID = 17, ExecInst = 18, LastPx = 31, LastQty = 32,
    OrderID = 37, OrderQty = 38, OrdStatus = 39, OrdType = 40, OrigClOrdID = 41, Price = 44, Side = 54,
    Symbol = 55, Text = 58, TimeInForce = 59, TransactTime = 60, RawDataLength = 95, RawData = 96,
    EncryptMethod = 98, StopPx = 99, CxlRejReason = 102, OrdRejReason = 103, HeartBtInt = 108,
    ExpireTime = 126, ResetSeqNumFlag = 141, ExecType = 150, LeavesQty = 151, CashOrderQty = 152,
    RefSeqNum = 45, RefMsgType = 372, BusinessRejectRefID = 379, BusinessRejectReason = 380,
    ExecRestatementReason = 378, CxlRejResponseTo = 434, Username = 553, Password = 554,
    TradeID = 1003, AggressorIndicator = 1057, DefaultApplVerID = 1137,
    NoMiscFees = 136, MiscFeeAmt = 137, MiscFeeCurr = 138, MiscFeeType = 139, MiscFeeBasis = 891,
    SelfTradeType = 7928, CancelOrdersOnDisconnect = 8013, BatchID = 8014, DropCopyFlag = 9406,
  };

  struct FixMsg
  {
    std::vector<std::pair<int, std::string>> fields;

    FixMsg() = default;
    explicit FixMsg(std::string msg_type) { fields.emplace_back(MsgType, std::move(msg_type)); }

    const std::string *find(int tag) const
    {
      for (const auto &f : fields)
        if (f.first == tag)
          return &f.second;
      return nullptr;
    }
    bool has(int tag) const { return find(tag) != nullptr; }
    std::string get(int tag, const std::string &dflt = {}) const
    {
      auto p = find(tag);
      return p ? *p : dflt;
    }
    char get_char(int tag, char dflt = 0) const
    {
      auto p = find(tag);
      return p && !p->empty() ? (*p)[0] : dflt;
    }
    std::string type() const { return get(MsgType); }

    // Replace the first occurrence, or append.
    FixMsg &set(int tag, std::string v)
    {
      for (auto &f : fields)
        if (f.first == tag)
        {
          f.second = std::move(v);
          return *this;
        }
      fields.emplace_back(tag, std::move(v));
      return *this;
    }
    FixMsg &set(int tag, char c) { return set(tag, std::string(1, c)); }
    // Always append (repeating groups).
    FixMsg &add(int tag, std::string v)
    {
      fields.emplace_back(tag, std::move(v));
      return *this;
    }

    // "8=FIXT.1.1|9=..|35=8|...|10=123|" with `sep`. nullopt if a field is not
    // tag=value, or BodyLength or CheckSum (when present) are wrong.
    static std::optional<FixMsg> parse(std::string_view raw, char sep = '\x01')
    {
      FixMsg m;
      size_t pos = 0, body_start = std::string_view::npos, sum_at = std::string_view::npos;
      long body_len = -1;
      while (pos < raw.size())
      {
        size_t end = raw.find(sep, pos);
        if (end == std::string_view::npos)
          end = raw.size();
        auto f = raw.substr(pos, end - pos);
        size_t eq = f.find('=');
        if (eq == std::string_view::npos || eq == 0)
          return std::nullopt;
        int tag = 0;
        for (size_t i = 0; i < eq; ++i)
        {
          if (f[i] < '0' || f[i] > '9')
            return std::nullopt;
          tag = tag * 10 + (f[i] - '0');
        }
        if (tag == BodyLength)
        {
          body_len = std::strtol(std::string(f.substr(eq + 1)).c_str(), nullptr, 10);
          body_start = end + 1;
        }
        if (tag == CheckSum)
          sum_at = pos;
        m.fields.emplace_back(tag, std::string(f.substr(eq + 1)));
        pos = end + 1;
      }
      if (body_len >= 0 && sum_at != std::string_view::npos)
      {
        if (long(sum_at - body_start) != body_len)
          return std::nullopt;
        unsigned sum = 0;
        for (size_t i = 0; i < sum_at; ++i)
          sum += static_cast<unsigned char>(sep == '\x01' ? raw[i] : (raw[i] == sep ? '\x01' : raw[i]));
        if (std::strtol(m.get(CheckSum).c_str(), nullptr, 10) != long(sum % 256))
          return std::nullopt;
      }
      return m;
    }

    // The message on the wire: BeginString, BodyLength, every field except
    // 8, 9 and 10 in order, CheckSum.
    std::string to_wire(const std::string &begin = "FIXT.1.1", char sep = '\x01') const
    {
      std::string body;
      for (const auto &f : fields)
        if (f.first != BeginString && f.first != BodyLength && f.first != CheckSum)
          body += std::to_string(f.first) + "=" + f.second + sep;
      std::string head = "8=" + begin + sep + "9=" + std::to_string(body.size()) + sep;
      std::string all = head + body;
      unsigned sum = 0;
      for (char c : all)
        sum += static_cast<unsigned char>(c == sep ? '\x01' : c);
      char cs[8];
      std::snprintf(cs, sizeof(cs), "%03u", sum % 256);
      return all + "10=" + cs + sep;
    }

    // Fields joined with '|', for logs and tests (no header or trailer added).
    std::string str() const
    {
      std::string s;
      for (const auto &f : fields)
        s += std::to_string(f.first) + "=" + f.second + "|";
      return s;
    }
  };
}
