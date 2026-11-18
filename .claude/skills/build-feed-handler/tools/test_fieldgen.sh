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
