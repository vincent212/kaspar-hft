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
