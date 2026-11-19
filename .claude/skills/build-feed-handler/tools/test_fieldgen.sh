#!/bin/sh
# Test fieldgen.py: generate and run the example tables' tests, then check that
# malformed tables are rejected with an error naming the line.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
gen="$here/tools/fieldgen.py"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

for t in itch50 soupbintcp; do
  python3 "$gen" "$here/examples/$t.tbl" --check
  python3 "$gen" "$here/examples/$t.tbl" -o "$out/$t.hpp"
done
for t in itch50 soupbintcp; do
  ${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$here/examples/${t}_test.cpp" -o "$out/${t}_test"
  "$out/${t}_test"
done

# reject <expected message fragment> < table
reject() {
  cat > "$out/bad.tbl"
  if msg=$(python3 "$gen" "$out/bad.tbl" --check 2>&1); then
    echo "FAIL: accepted a table that should fail with: $1"; exit 1
  fi
  case "$msg" in
    *"line "*"$1"*) ;;
    *) echo "FAIL: expected a line number and '$1', got: $msg"; exit 1 ;;
  esac
}

head='@venue v
@endian big
@type I uint
@type A char
@type B bytes'

reject "expected 5" <<T
$head
### x::from_v
MessageType A 0 1 # M
F1 I 1 4
F2 I 3 4
T
reject "@size says" <<T
$head
### x::from_v
MessageType A 0 1 # M
@size 4
F1 I 1 4
T
reject "is not a number" <<T
$head
### x::from_v
MessageType A 0 1 # M
F1 I 1 4x
T
reject "takes one argument" <<T
@venue
T
reject "before the section's first message" <<T
$head
### x::from_v
MessageType A 0 1 # M

@length_prefix 1 exclusive
MessageType B 0 1 # N
T
reject "differ in their length prefix" <<T
$head
### x::from_v
MessageType A 0 1 # M

MessageType B 0 1 # N
@length_prefix 1 exclusive
T
reject "already used" <<T
$head
### x::from_v
MessageType A 0 1 # M

MessageType A 0 1 # N
T
reject "not a usable C++ identifier" <<T
$head
### x::from_v
MessageType A 0 1 # M
not A 1 1
T
reject "not a usable C++ identifier" <<T
$head
### x::from_v
MessageType A 0 1 # M
type A 1 1
T
reject "name of the generated length prefix" <<T
$head
### x::from_v
@length_prefix 2 exclusive
MessageType A 0 1 # M
MessageLength I 1 2
T
reject "must be last" <<T
$head
### x::from_v
MessageType A 0 1 # M
Tail B 1
F I 1 4
T

reject "generator uses" <<T
$head
### x::from_v
MessageType A 0 1 # decoded
T
reject "C++ keyword" <<T
$head
### x::from_v
MessageType A 0 1 # delete
T
reject "already declared" <<T
$head
### x::from_v
MessageType A 0 1 # M
### x::from_v
MessageType B 0 1 # N
T
reject "lower-case identifier" <<T
@venue CME
@endian big
T
reject "one character of length 1" <<T
$head
### x::from_v
MessageType AB 0 2 # M
T

# Type codes that need escaping, a literal '-' code, and a description ending
# in a backslash all compile and round-trip.
cat > "$out/odd.tbl" <<'T'
@venue odd
@endian little
@type I uint
@type A char
### x::from_odd
@length_prefix 2 exclusive
MessageType ' 0 1 # Quote
F I 1 4 # ends in a backslash \
G I 5 4 # next field

MessageType \ 0 1 # Backslash

MessageType - 0 1 # Dash
### x::to_odd
MessageType - 0 1 # DashOut
F I 1 4 # x
### end
T
python3 "$gen" "$out/odd.tbl" -o "$out/odd.hpp"
cat > "$out/odd.cpp" <<'C'
#include "odd.hpp"
#include <cassert>
int main()
{
  using namespace odd::x;
  static_assert(from_odd::Quote::type == '\'' && from_odd::Backslash::type == '\\');
  static_assert(sizeof(from_odd::Quote) == 2 + 9 && offsetof(from_odd::Quote, G) == 7);
  char buf[16];
  assert(to_odd::encode(buf, sizeof buf, to_odd::DashOut{}) == 5 && buf[0] == '-');
  char q[] = {1, 0, '\\'};
  assert(from_odd::decode(q, 3) && from_odd::decode(q, 3).size == 3);
  return 0;
}
C
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$out/odd.cpp" -o "$out/odd"
"$out/odd"

reject "is not a number" <<T
$head
### x::from_v
@type_code uint 2
MessageType A 0 2 # M
T
reject "@type_code says" <<T
$head
### x::from_v
@type_code uint 2
MessageType 100 0 1 # M
T
reject "before its first message" <<T
$head
### x::from_v
MessageType A 0 1 # M

@type_code uint 2
T
reject "does not fit" <<T
$head
### x::from_v
@type_code uint 1
MessageType 300 0 1 # M
T

# Numeric type codes after an inclusive size, big-endian, as in a T7-style feed.
cat > "$out/num.tbl" <<'T'
@venue numv
@endian big
@type I uint
### t7::from_numv
@length_prefix 2 inclusive
@type_code uint 2
MessageType 13100 0 2 # OrderAdd
@size 6
Id I 2 4 # id

MessageType 0x3334 0 2 # OrderDelete
Id I 2 4 # id
### end
T
python3 "$gen" "$out/num.tbl" -o "$out/num.hpp"
cat > "$out/num.cpp" <<'C'
#include "num.hpp"
#include <cassert>
int main()
{
  using namespace numv::t7::from_numv;
  unsigned char a[] = {0, 8, 0x33, 0x2c, 0, 0, 0, 5, 0, 8, 0x33, 0x34, 0, 0, 0, 6};
  auto d = decode((const char *)a, sizeof a);
  assert(d && d.size == 8 && std::get<OrderAdd>(d.msg).Id == 5u);
  auto e = decode((const char *)a + 8, 8);
  assert(e && std::get<OrderDelete>(e.msg).Id == 6u);
  // An inclusive prefix smaller than itself gives nothing to skip by: size 0.
  unsigned char z[] = {0, 0, 0x33, 0x2c}, one[] = {0, 1, 0x33, 0x2c};
  auto mz = decode((const char *)z, sizeof z), mo = decode((const char *)one, sizeof one);
  assert(mz.st == numv::status::malformed && mz.size == 0 && mo.st == numv::status::malformed && mo.size == 0);
  // One not smaller than itself but shorter than the type code is skipped by.
  unsigned char two[] = {0, 2, 0x33, 0x2c};
  assert(decode((const char *)two, sizeof two).size == 2);
  return 0;
}
C
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$out/num.cpp" -o "$out/num"
"$out/num"

reject "no earlier section" <<T
$head
### x::from_v
@include y::from_v
MessageType A 0 1 # M
T
reject "below the section's @offset_base" <<T
$head
### x::from_v
@offset_base 2
MessageType A 2 1 # M
F I 1 4 # f
T
reject "already used" <<T
$head
### x::from_v
MessageType A 0 1 # M
### y::from_v
@include x::from_v
MessageType A 0 1 # N
T

# One message layer under two framings, and a snapshot layer that reuses it:
# ITCH without a length (as in SoupBinTCP), decoded also with MoldUDP64's 2-byte
# length; GLIMPSE = the ITCH messages plus End of Snapshot. The SoupBinTCP-style
# section copies the spec's offsets, which count the 2-byte length field.
cat > "$out/layers.tbl" <<'T'
@venue lay
@endian big
@type I uint
@type A char
### itch::from_lay
@framed_decode mold 2 exclusive
@framed_decode incl 2 inclusive
MessageType A 0 1 # AddOrder
Ref I 1 4 # r

MessageType D 0 1 # DeleteOrder
Ref I 1 4 # r
### glimpse::from_lay
@include itch::from_lay
MessageType G 0 1 # EndOfSnapshot
Seq A 1 20 # s
### soup::from_lay
@length_prefix 2 exclusive
@offset_base 2
MessageType S 2 1 # SequencedData
Payload B 3 # p
### end
T
sed -i.bak 's/^@type A char$/@type A char\
@type B bytes/' "$out/layers.tbl"
python3 "$gen" "$out/layers.tbl" -o "$out/layers.hpp"
cat > "$out/layers.cpp" <<'C'
#include "layers.hpp"
#include <cassert>
int main()
{
  using namespace lay;
  unsigned char raw[] = {'A', 0, 0, 0, 7};
  auto a = itch::from_lay::decode((const char *)raw, sizeof raw);
  assert(a && a.size == 5 && std::get<itch::from_lay::AddOrder>(a.msg).Ref == 7u);
  unsigned char framed[] = {0, 5, 'D', 0, 0, 0, 9, 0, 9, '?', 0, 0, 0, 0, 0, 0, 0, 0};
  auto d = itch::from_lay::decode_mold((const char *)framed, sizeof framed);
  assert(d && d.size == 7 && std::get<itch::from_lay::DeleteOrder>(d.msg).Ref == 9u);
  auto u = itch::from_lay::decode_mold((const char *)framed + 7, sizeof framed - 7);
  assert(u.st == status::unknown && u.size == 11);
  unsigned char shortp[] = {0, 3, 'A', 0, 0};
  assert(itch::from_lay::decode_mold((const char *)shortp, sizeof shortp).st == status::malformed);
  unsigned char incl[] = {0, 7, 'D', 0, 0, 0, 9}, incl1[] = {0, 1, 'D', 0, 0, 0, 9};
  auto di = itch::from_lay::decode_incl((const char *)incl, sizeof incl);
  assert(di && di.size == 7 && std::get<itch::from_lay::DeleteOrder>(di.msg).Ref == 9u);
  auto mi = itch::from_lay::decode_incl((const char *)incl1, sizeof incl1);
  assert(mi.st == status::malformed && mi.size == 0);
  auto g = glimpse::from_lay::decode((const char *)raw, sizeof raw);
  assert(g && std::holds_alternative<itch::from_lay::AddOrder>(g.msg));
  static_assert(std::is_same_v<glimpse::from_lay::AddOrder, itch::from_lay::AddOrder>);
  static_assert(offsetof(soup::from_lay::SequencedData, MessageType) == 2);
  return 0;
}
C
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$out/layers.cpp" -o "$out/layers"
"$out/layers"
python3 "$gen" "$out/layers.tbl" --markdown | grep -q "Also decodes the messages of"

# A comment line inside a message does not end it.
cat > "$out/ok.tbl" <<T
$head
### x::from_v
MessageType A 0 1 # M
# continued on the next page
F I 1 4
T
python3 "$gen" "$out/ok.tbl" --check >/dev/null

# A message may have the name of one of its fields (the structs have no
# constructor, so C++ allows a data member named like its class).
cat > "$out/same.tbl" <<'T'
@venue same
@endian big
@type I uint
@type A char
### x::from_same
MessageType A 0 1 # Symbol
Symbol A 1 8 # s
### x::to_same
MessageType B 0 1 # Qty
Qty I 1 4 # q
### end
T
python3 "$gen" "$out/same.tbl" -o "$out/same.hpp"
cat > "$out/same.cpp" <<'C'
#include "same.hpp"
#include <cassert>
int main()
{
  using namespace same::x;
  char b[9] = {'A', 'I', 'B', 'M'};
  auto d = from_same::decode(b, sizeof b);
  assert(d && std::get<from_same::Symbol>(d.msg).get_Symbol() == "IBM");
  to_same::Qty q{};
  q.Qty = 7;
  char o[8];
  assert(to_same::encode(o, sizeof o, q) == 5 && o[4] == 7);
  return 0;
}
C
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$out/same.cpp" -o "$out/same"
"$out/same"

# @import: a table reuses sections of another file. base.tbl is a venue's
# message layer and its packet header; snap.tbl (same venue) decodes the base
# messages plus its own, as GLIMPSE does ITCH; other.tbl (another venue)
# includes the base layer too. Each section is emitted under a guard named
# after it and a hash of its code, so including several of these headers in
# one translation unit compiles each section once.
mkdir -p "$out/imp/sub"
cat > "$out/imp/sub/base.tbl" <<'T'
@venue imp
@endian big
@type I uint
@type A char
### pkt::from_imp
MessageType - 0 0 # UnitHeader
Length I 0 2 # packet length
Count I 2 1 # messages

### itch::from_imp
MessageType A 0 1 # AddOrder
Ref I 1 4 # r

MessageType D 0 1 # DeleteOrder
Ref I 1 4 # r
### end
T
cat > "$out/imp/snap.tbl" <<'T'
@venue imp
@endian big
@import sub/base.tbl itch::from_imp
@type I uint
### glimpse::from_imp
@include itch::from_imp
MessageType G 0 1 # EndOfSnapshot
Seq I 1 8 # s
### end
T
cat > "$out/imp/other.tbl" <<'T'
@venue oth
@endian big
@import sub/base.tbl
@type I uint
### feed::from_oth
@include itch::from_imp
MessageType X 0 1 # Extra
V I 1 2 # v
### end
T
python3 "$gen" "$out/imp/sub/base.tbl" -o "$out/base.hpp"
python3 "$gen" "$out/imp/snap.tbl" --check | grep -q "1 imported sections"
python3 "$gen" "$out/imp/snap.tbl" -o "$out/snap.hpp"
python3 "$gen" "$out/imp/other.tbl" -o "$out/other.hpp"
if grep -q "UnitHeader" "$out/snap.hpp"; then echo "FAIL: @import of one section emitted others"; exit 1; fi
grep -q "UnitHeader" "$out/other.hpp" || { echo "FAIL: @import of a whole file missed a section"; exit 1; }
python3 "$gen" "$out/imp/snap.tbl" --markdown | grep -q "Imports .itch::from_imp. from .sub/base.tbl."
cat > "$out/imp.cpp" <<'C'
#include "base.hpp"
#include "snap.hpp"
#include "other.hpp"
#include <cassert>
int main()
{
  static_assert(std::is_same_v<imp::glimpse::from_imp::AddOrder, imp::itch::from_imp::AddOrder>);
  static_assert(std::is_same_v<oth::feed::from_oth::DeleteOrder, imp::itch::from_imp::DeleteOrder>);
  static_assert(sizeof(imp::pkt::from_imp::UnitHeader) == 3);
  unsigned char a[] = {'A', 0, 0, 0, 7};
  auto g = imp::glimpse::from_imp::decode((const char *)a, sizeof a);
  assert(g && std::get<imp::itch::from_imp::AddOrder>(g.msg).Ref == 7u);
  unsigned char x[] = {'X', 1, 2};
  auto o = oth::feed::from_oth::decode((const char *)x, sizeof x);
  assert(o && std::get<oth::feed::from_oth::Extra>(o.msg).V == 0x102u);
  return 0;
}
C
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$out/imp.cpp" -o "$out/imp_test"
"$out/imp_test"
# Two different versions of one section in one translation unit do not compile.
sed 's/^Ref I 1 4 # r$/Ref I 1 4 # reference/' "$out/imp/sub/base.tbl" > "$out/imp/sub/base2.tbl"
mv "$out/imp/sub/base2.tbl" "$out/imp/sub/base.tbl"
python3 "$gen" "$out/imp/snap.tbl" -o "$out/snap.hpp"
if ${CXX:-c++} -std=c++20 -fsyntax-only -I"$out" "$out/imp.cpp" 2>/dev/null; then
  echo "FAIL: two versions of an imported section compiled together"; exit 1
fi

# reject_in <dir> <expected message fragment> < table: as reject, for a table
# that imports files next to it.
reject_in() {
  cat > "$1/bad.tbl"
  if msg=$(python3 "$gen" "$1/bad.tbl" --check 2>&1); then
    echo "FAIL: accepted a table that should fail with: $2"; exit 1
  fi
  case "$msg" in
    *"line "*"$2"*) ;;
    *) echo "FAIL: expected a line number and '$2', got: $msg"; exit 1 ;;
  esac
}
reject_in "$out/imp" "no such file" <<T
$head
@import nothere.tbl
T
reject_in "$out/imp" "has no section" <<T
$head
@import sub/base.tbl itch::from_nobody
T
reject_in "$out/imp" "before the first section" <<T
$head
### x::from_v
@import sub/base.tbl
T
reject_in "$out/imp" "already defined by an imported table" <<T
@venue imp
@endian big
@type A char
@import sub/base.tbl
### itch::from_imp
MessageType Z 0 1 # Z
T
reject_in "$out/imp" "no earlier section of that name" <<T
@venue imp
@endian big
@type A char
@import sub/base.tbl itch::from_imp
### glimpse::from_imp
@include pkt::from_imp
MessageType Z 0 1 # Z
T
cat > "$out/imp/cyc_a.tbl" <<'T'
@venue imp
@endian big
@import cyc_b.tbl
T
cat > "$out/imp/cyc_b.tbl" <<'T'
@venue imp
@endian big
@import cyc_a.tbl
T
if msg=$(python3 "$gen" "$out/imp/cyc_a.tbl" --check 2>&1); then echo "FAIL: accepted an @import cycle"; exit 1; fi
case "$msg" in *"@import cycle"*) ;; *) echo "FAIL: expected an @import cycle, got: $msg"; exit 1 ;; esac
reject_in "$out/imp" "little-endian" <<T
@venue imp
@endian little
@type A char
@import sub/base.tbl itch::from_imp
### glimpse::from_imp
@include itch::from_imp
MessageType Z 0 1 # Z
T

# @header: fields that precede every message, the type code inside them and
# the message length in a header field (Aquis: type, length, seqNo).
cat > "$out/hdr.tbl" <<'T'
@venue hdr
@endian little
@type u8 uint
@type u32 uint
@type u64 uint
@type C char
### md::from_hdr
@type_code uint 1
@header Header
MessageType code 0 1 # msgType
length u8 1 1 # the whole message, header included
seqNo u32 2 4 # sequence number
@message_length length whole

MessageType 1 0 1 # Heartbeat
@size 6

MessageType 2 0 1 # OrderAdd
@size 15
orderId u64 6 8 # order
side C 14 1 # B or S
### md::to_hdr
@type_code uint 1
@header Header
MessageType code 0 1 # msgType
length u8 1 1 # the whole message
seqNo u32 2 4 # sequence number
@message_length length whole

MessageType 2 0 1 # OrderAdd
orderId u64 6 8 # order
side C 14 1 # B or S
### end
T
python3 "$gen" "$out/hdr.tbl" -o "$out/hdr.hpp"
python3 "$gen" "$out/hdr.tbl" --markdown | grep -q "Every message starts with the header Header (6 bytes), whose .length. is the length of the whole message"
# Currenex OUCH: SequenceNumber, Timestamp, then a character type code; no length.
cat > "$out/cnx.tbl" <<'T'
@venue cnx
@endian big
@type Integer int
@type Alpha char
### ouch::from_cnx
@header Header
SequenceNumber Integer 0 4 # session sequence number
Timestamp Integer 4 4 # ms past midnight
MessageType code 8 1 # type

MessageType C 8 1 # Heartbeat
SessionID Integer 9 4 # session
### end
T
python3 "$gen" "$out/cnx.tbl" -o "$out/cnx.hpp"
cat > "$out/hdr.cpp" <<'C'
#include "hdr.hpp"
#include "cnx.hpp"
#include <cassert>
int main()
{
  using namespace hdr::md;
  static_assert(sizeof(from_hdr::Header) == 6 && sizeof(from_hdr::OrderAdd) == 15);
  static_assert(offsetof(from_hdr::OrderAdd, seqNo) == 2 && offsetof(from_hdr::OrderAdd, orderId) == 6);
  // An OrderAdd two bytes longer than the table (a later version), then an
  // unknown type of 7 bytes, then a heartbeat.
  unsigned char b[] = {2, 17, 9, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 'B', 0xee, 0xee,
                       99, 7, 0, 0, 0, 0, 0xee,
                       1, 6, 10, 0, 0, 0};
  auto a = from_hdr::decode((const char *)b, sizeof b);
  assert(a && a.size == 17);
  const auto &o = std::get<from_hdr::OrderAdd>(a.msg);
  assert(o.orderId == 5u && o.seqNo == 9u && o.side == 'B');
  auto u = from_hdr::decode((const char *)b + 17, sizeof b - 17);
  assert(u.st == hdr::status::unknown && u.size == 7);
  auto h = from_hdr::decode((const char *)b + 24, sizeof b - 24);
  assert(h && h.size == 6 && std::get<from_hdr::Heartbeat>(h.msg).seqNo == 10u);
  assert(from_hdr::decode((const char *)b, 16).st == hdr::status::incomplete);
  assert(from_hdr::decode((const char *)b, 5).st == hdr::status::incomplete);
  unsigned char shortlen[] = {2, 3, 0, 0, 0, 0};
  auto m = from_hdr::decode((const char *)shortlen, sizeof shortlen);
  assert(m.st == hdr::status::malformed && m.size == 0);
  unsigned char shortmsg[] = {2, 8, 0, 0, 0, 0, 0, 0};
  auto s = from_hdr::decode((const char *)shortmsg, sizeof shortmsg);
  assert(s.st == hdr::status::malformed && s.size == 8);
  to_hdr::OrderAdd e{};
  e.seqNo = 3;
  e.orderId = 4;
  e.side = 'S';
  char w[32];
  assert(to_hdr::encode(w, sizeof w, e) == 15 && w[0] == 2 && w[1] == 15);
  auto r = from_hdr::decode(w, 15);
  assert(r && std::get<from_hdr::OrderAdd>(r.msg).orderId == 4u);

  using namespace cnx::ouch::from_cnx;
  static_assert(offsetof(Heartbeat, MessageType) == 8 && sizeof(Heartbeat) == 13);
  unsigned char c[] = {0, 0, 0, 1, 0, 0, 0, 2, 'C', 0, 0, 0, 3, 'Z'};
  auto hb = decode((const char *)c, sizeof c);
  assert(hb && hb.size == 13 && std::get<Heartbeat>(hb.msg).SessionID == 3);
  assert(std::get<Heartbeat>(hb.msg).SequenceNumber == 1);
  unsigned char z[] = {0, 0, 0, 1, 0, 0, 0, 2, 'Z'};
  auto uz = decode((const char *)z, sizeof z);
  assert(uz.st == cnx::status::unknown && uz.size == 0);
  return 0;
}
C
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$out/hdr.cpp" -o "$out/hdr"
"$out/hdr"

hh='@venue v
@endian big
@type I uint
@type A char'
reject "before its first message" <<T
$hh
### x::from_v
MessageType A 0 1 # M

@header H
MessageType code 0 1 # t
T
reject "no MessageType code row" <<T
$hh
### x::from_v
@header H
Len I 0 2 # l

MessageType A 0 1 # M
T
reject "no such field in the @header" <<T
$hh
### x::from_v
@header H
MessageType code 0 1 # t
Len I 1 2 # l
@message_length Length whole

MessageType A 0 1 # M
T
reject "the @header puts the type code at offset 0" <<T
$hh
### x::from_v
@header H
MessageType code 0 1 # t
Len I 1 2 # l

MessageType A 3 1 # M
F I 3 4 # f
T
reject "has no @length_prefix" <<T
$hh
### x::from_v
@length_prefix 2 exclusive
@header H
MessageType code 0 1 # t

MessageType A 0 1 # M
T
reject "expected 3" <<T
$hh
### x::from_v
@header H
MessageType code 0 1 # t
Len I 1 2 # l

MessageType A 0 1 # M
F I 4 4 # f
T
reject "repeated, or clashes" <<T
$hh
### x::from_v
@header H
MessageType code 0 1 # t
Len I 1 2 # l

MessageType A 0 1 # M
Len I 3 2 # again
T
reject "@type_code says 2" <<T
$hh
### x::from_v
@type_code uint 2
@header H
MessageType code 0 1 # t

MessageType 5 0 2 # M
T

# @by_length: one type code, two layouts told apart by the message length
# (NYSE Pillar's 16- and 8-byte Refresh Header; 2-byte MsgSize counting itself).
cat > "$out/bylen.tbl" <<'T'
@venue byl
@endian little
@type B uint
### ctl::from_byl
@length_prefix 2 inclusive
@type_code uint 2
MessageType 35 0 2 # RefreshHeader
@size 14
CurrentRefreshPkt B 2 2 # c
TotalRefreshPkts B 4 2 # t
LastSeqNum B 6 4 # l
LastSymbolSeqNum B 10 4 # s

MessageType 35 0 2 # RefreshHeaderShort
@by_length
@size 6
CurrentRefreshPkt B 2 2 # c
TotalRefreshPkts B 4 2 # t

MessageType 32 0 2 # SymbolClear
SeqNum B 2 4 # s
### end
T
python3 "$gen" "$out/bylen.tbl" -o "$out/bylen.hpp"
python3 "$gen" "$out/bylen.tbl" --markdown | grep -q "told apart from the other layouts"
cat > "$out/bylen.cpp" <<'C'
#include "bylen.hpp"
#include <cassert>
int main()
{
  using namespace byl::ctl::from_byl;
  unsigned char full[] = {16, 0, 35, 0, 1, 0, 3, 0, 9, 0, 0, 0, 4, 0, 0, 0};
  auto f = decode((const char *)full, sizeof full);
  assert(f && f.size == 16 && std::get<RefreshHeader>(f.msg).LastSeqNum == 9u);
  unsigned char sh[] = {8, 0, 35, 0, 2, 0, 3, 0};
  auto s = decode((const char *)sh, sizeof sh);
  assert(s && s.size == 8 && std::get<RefreshHeaderShort>(s.msg).CurrentRefreshPkt == 2u);
  unsigned char tiny[] = {6, 0, 35, 0, 2, 0};
  auto t = decode((const char *)tiny, sizeof tiny);
  assert(t.st == byl::status::malformed && t.size == 6);
  unsigned char other[] = {8, 0, 32, 0, 1, 0, 0, 0};
  assert(std::holds_alternative<SymbolClear>(decode((const char *)other, sizeof other).msg));
  return 0;
}
C
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$out/bylen.cpp" -o "$out/bylen"
"$out/bylen"

reject "no earlier message" <<T
$head
### x::from_v
@length_prefix 2 exclusive
MessageType A 0 1 # M
@by_length
F I 1 4 # f
T
reject "needs the message length" <<T
$head
### x::from_v
MessageType A 0 1 # M
F I 1 4 # f

MessageType A 0 1 # N
@by_length
T
reject "same size cannot be told apart" <<T
$head
### x::from_v
@length_prefix 2 exclusive
MessageType A 0 1 # M
F I 1 4 # f

MessageType A 0 1 # N
@by_length
G I 1 4 # g
T
reject "has a variable-length field" <<T
$head
### x::from_v
@length_prefix 2 exclusive
MessageType A 0 1 # M
F I 1 4 # f

MessageType A 0 1 # N
@by_length
Tail B 1
T
# @include is transitive: c includes b, which includes a; c decodes a's messages.
cat > "$out/inc.tbl" <<'T'
@venue inc
@endian big
@type I uint
@type A char
### a::from_inc
MessageType A 0 1 # FromA
Ref I 1 4 # r
### b::from_inc
@include a::from_inc
MessageType B 0 1 # FromB
Ref I 1 4 # r
### c::from_inc
@include b::from_inc
MessageType C 0 1 # FromC
Ref I 1 4 # r
### d::from_inc
@include a::from_inc
@include b::from_inc
MessageType D 0 1 # FromD
Ref I 1 4 # r
### end
T
python3 "$gen" "$out/inc.tbl" -o "$out/inc.hpp"
cat > "$out/inc.cpp" <<'C'
#include "inc.hpp"
#include <cassert>
int main()
{
  using namespace inc;
  unsigned char ra[] = {'A', 0, 0, 0, 3}, rb[] = {'B', 0, 0, 0, 4};
  auto x = c::from_inc::decode((const char *)ra, sizeof ra);
  assert(x && std::get<a::from_inc::FromA>(x.msg).Ref == 3u);
  auto y = c::from_inc::decode((const char *)rb, sizeof rb);
  assert(y && std::get<b::from_inc::FromB>(y.msg).Ref == 4u);
  static_assert(std::is_same_v<c::from_inc::FromA, a::from_inc::FromA>);
  static_assert(std::variant_size_v<d::from_inc::message_t> == 3);
  return 0;
}
C
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$out/inc.cpp" -o "$out/inc"
"$out/inc"

# expect_sections <n> < table: the table checks and has n sections.
expect_sections() {
  cat > "$out/sec.tbl"
  msg=$(python3 "$gen" "$out/sec.tbl" --check 2>&1) || { echo "FAIL: $msg"; exit 1; }
  case "$msg" in
    *": $1 sections, $2 messages, OK") ;;
    *) echo "FAIL: expected $1 sections and $2 messages, got: $msg"; exit 1 ;;
  esac
}
# An indented "###" line is a section header, not a comment.
expect_sections 2 2 <<T
$head
### x::from_v
MessageType A 0 1 # M

   ### y::from_v
MessageType A 0 1 # N
T
# A banner, a "###" without a space and a "####" heading are comments.
expect_sections 1 2 <<T
$head
### x::from_v
##########################
MessageType A 0 1 # M
###y::from_v
#### y::from_v

MessageType B 0 1 # N
T
reject "section must be named" <<T
$head
  ### Messages
T

reject "has no fields" <<T
$head
### x::to_v
MessageType - 0 0 # Header
T
reject "has no fields" <<T
$head
### x::to_v
MessageType - 0 0 # Header
Tail B 0
T
for f in be uint_n set_str get_str uint32_t; do
  reject "would hide the generated helper $f" <<T
$head
### x::from_v
MessageType A 0 1 # M
$f I 1 4
T
done
reject "would hide the generated helper set_str" <<T
$head
### x::from_v
MessageType A 0 1 # M
str A 1 4
T
reject "member set_Name repeated" <<T
$head
### x::from_v
MessageType A 0 1 # M
Name A 1 4
set_Name I 5 4
T
reject "member get_Name repeated" <<T
$head
### x::from_v
MessageType A 0 1 # M
get_Name I 1 4
Name A 5 4
T
reject "@framed_decode mold already declared" <<T
$head
### x::from_v
@framed_decode mold 2 exclusive
@framed_decode mold 2 inclusive
MessageType A 0 1 # M
T
# A field named set_X beside a non-text field X generates no clash.
cat > "$out/ok2.tbl" <<T
$head
### x::from_v
MessageType A 0 1 # M
Name I 1 4
set_Name I 5 4
T
python3 "$gen" "$out/ok2.tbl" --check >/dev/null
echo "rejection tests OK"
