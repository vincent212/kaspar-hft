/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// CxCodec: what the header generated from currenex_ust_ouch.tbl does not
// cover: the SOH/ETX framing and the message header around every message,
// units, reason codes, and writing a Currenex-side message as bytes (for the
// simulator and the test fake, which play Currenex).
//
// On the wire every message is (UST OUCH 4.1, p.6; 9, p.17):
//
//   SOH (0x01) | SequenceNumber (4) | Timestamp (4) | MessageType (1) | body | ETX (0x03)
//
// The generated message structs start at MessageType; Header holds the two
// fields before it. The spec defines no escaping: binary fields may contain
// 0x01 and 0x03, so a message is delimited by its type's fixed length, and
// SOH and ETX are only checked where they must be.

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "currenex_ust/gen/currenex_ust_ouch.hpp"

namespace currenex::ouch
{
  namespace to = to_currenex;
  namespace from = from_currenex;
  using Header = to_currenex_header::Header;

  constexpr char SOH = 0x01;
  constexpr char ETX = 0x03;
  constexpr size_t HEADER_SIZE = sizeof(Header); // before MessageType
  constexpr size_t FRAME_OVERHEAD = 1 + HEADER_SIZE + 1;

  // Amounts: Long, 2 implied decimals (UST OUCH 4.2.2, p.6). Rates: Long,
  // 9 implied decimals (4.2.3, p.6).
  constexpr int64_t AMOUNT_SCALE = 100;
  constexpr int64_t RATE_SCALE = 1'000'000'000;

  constexpr int16_t VERSION = 200;                 // Logon Version (UST OUCH 10.1, p.18)
  constexpr int32_t NOT_ROUTED = -1;               // RoutedUserKey when not routing (7.1, p.13)
  constexpr int MS_PER_DAY = 86'400'000;

  // One message on the wire: SOH, the header, the message from its type
  // code (the bytes encode() or to_bytes() produced), ETX.
  inline std::string to_frame(int32_t seq, int32_t ts_ms, std::string_view msg)
  {
    Header h{};
    h.SequenceNumber = seq;
    h.Timestamp = ts_ms;
    std::string s;
    s.reserve(FRAME_OVERHEAD + msg.size());
    s.push_back(SOH);
    s.append(reinterpret_cast<const char *>(&h), sizeof(h));
    s.append(msg);
    s.push_back(ETX);
    return s;
  }

  // A client message (to_currenex) as bytes from its type code: encode()
  // sets the type.
  template <class M>
  std::string encoded(const M &m)
  {
    char buf[sizeof(M)];
    size_t n = to::encode(buf, sizeof(buf), m);
    return std::string(buf, n);
  }

  // A Currenex message (from_currenex) as bytes from its type code; the
  // generated from_ structs have no encode, so the type is set here.
  template <class M>
  std::string to_bytes(M m)
  {
    m.MessageType = M::type;
    return std::string(reinterpret_cast<const char *>(&m), sizeof(m));
  }

  // The next message in a received byte stream.
  struct Frame
  {
    enum Status
    {
      ok,         // msg decoded; consumed is the whole frame, SOH to ETX
      incomplete, // read more and call again at the same place
      unknown,    // a type this table does not list: seq, ts and type are set; consumed skips to the next frame
      garbage,    // not at a frame (no SOH, or no ETX where the type's length ends it); consumed skips it
    };
    Status st = incomplete;
    size_t consumed = 0;
    int32_t seq = 0;
    int32_t ts = 0;
    char type = 0;
    const char *data = nullptr; // the message from its type code (ok)
    size_t len = 0;
    from::message_t msg;
  };

  // Decode the frame at buf. A frame of unknown type cannot be measured: it
  // is taken to end at the next ETX that is followed by SOH (or by the end
  // of a buffer of max_unknown bytes or more).
  Frame deframe(const char *buf, size_t len, size_t max_unknown = 2048);

  // Appendix A error codes (UST OUCH, p.35).
  const char *error_text(int code);
  // Appendix B logout reason codes (UST OUCH, p.35).
  const char *logout_text(std::string_view code);

  // Milliseconds past midnight UTC of epoch_ms, as the header carries it
  // (UST OUCH 4.2.4, p.7).
  inline int32_t ms_of_day(int64_t epoch_ms) { return int32_t(((epoch_ms % MS_PER_DAY) + MS_PER_DAY) % MS_PER_DAY); }
  int64_t epoch_ms_now();
}
