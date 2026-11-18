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
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$out/odd.cpp" -o "$out/odd" && "$out/odd"

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
  return 0;
}
C
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$out/num.cpp" -o "$out/num" && "$out/num"

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
  auto g = glimpse::from_lay::decode((const char *)raw, sizeof raw);
  assert(g && std::holds_alternative<itch::from_lay::AddOrder>(g.msg));
  static_assert(std::is_same_v<glimpse::from_lay::AddOrder, itch::from_lay::AddOrder>);
  static_assert(offsetof(soup::from_lay::SequencedData, MessageType) == 2);
  return 0;
}
C
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" "$out/layers.cpp" -o "$out/layers" && "$out/layers"
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
echo "rejection tests OK"
