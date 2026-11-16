#!/usr/bin/env bash
#
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.
#
# compare_books.sh -- replay one capture window through two `sim` builds and
# compare their books, top 64 levels, after every ADD and MOD.
#
#   compare_books.sh <sim A> <sim B> <capture.bin> <universe.json> <contract> \
#                    <t0 epoch ns> <t1 epoch ns> <probe date YYYYMMDD> [extra B args...]
#
# Both runs get the same arguments; anything after the probe date is passed to
# B only (e.g. --ob-ladder 4096). OB prints its book after every ADD and MOD
# from t0 (OB::debug_print_book: the BBO, then per level "A:<d> [px:size:count]"
# and every order in queue order, d being ticks from the touch), and the replay
# stops at t1.
#
# Each stderr stream is cut to the book lines -- the record header, the "OBX:"
# order lines and the levels with d < 64 -- and the two are compared byte for
# byte by cmp, which stops at the first difference. Nothing large is written.
# SOM's fill lines are compared afterwards.
#
# Typical use: A = sim built from main, B = sim built from this branch;
# STATES=1 when B runs the ring ladder (see books() below).
#
set -uo pipefail

[ $# -ge 8 ] || { sed -n '7,22p' "$0"; exit 2; }
SIM_A=$(realpath "$1"); SIM_B=$(realpath "$2"); CAP=$(realpath "$3")
UNI=$(realpath "$4"); CONTRACT=$5; T0=$6; T1=$7; DAY=$8
shift 8
HERE=$(cd "$(dirname "$0")" && pwd)
CONFIG=${CONFIG:-$HERE/../../sim/config_a}
LEVELS=${LEVELS:-64}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/obcmp.XXXXXX")
mkfifo "$WORK/a.fifo" "$WORK/b.fifo"
mkdir -p "$WORK/a" "$WORK/b"
echo "work dir: $WORK"

# STATES=1 compares the sequence of distinct book states instead of every
# print: consecutive identical books collapse to one, record headers and OBX
# lines are dropped, and real orders are identified by exchange order id.
# This is the comparison for a ring ladder (sim --ob-ladder). The ring drops
# ADDs below its window, so (1) OB numbers fewer orders and every later
# internal id shifts, and (2) a later cancel of a dropped order finds nothing
# and prints nothing, where the full ladder prints a record whose top levels
# did not change. Neither changes a book state. Real orders print as
# <id>:<exchange id>:<size>, ours as <id>S:<size>; ours keep the id SOM gives.
STATES=${STATES:-0}

books() {   # the book lines of an OB debug stream, levels cut at $LEVELS
  LC_ALL=C grep -a -E '^(sym=|OBX:|[AB]:[0-9]|=====)' |
    LC_ALL=C awk -v L="$LEVELS" -v X="$STATES" '
      function flush() { if (rec != "" && rec != last) { printf "%s--\n", rec; last = rec } rec = "" }
      /^[AB]:/ { split($1, a, ":"); if (a[2] + 0 >= L) next }
      X != 1 { print; next }
      /^OBX:/ { next }
      /^sym=/ { flush(); next }
      {
        for (i = 2; i <= NF; i++) if ($i ~ /^[0-9]+:[0-9]+:[0-9]+$/) sub(/^[0-9]+:/, "", $i)
        rec = rec $0 "\n"
      }
      END { flush() }'
}

run() {   # dir sim extra...
  local dir=$1 sim=$2; shift 2
  cd "$dir" && "$sim" --datafile "$CAP" --universe "$UNI" --contract "$CONTRACT" \
      --config "$CONFIG" --probe-size 10 --probe-date "$DAY" \
      --ob-debug "$T0" --end-ts "$T1" "$@" 2>&1 >out.txt | books |
      tee >(wc -l > ../$(basename "$dir").lines)
}

run "$WORK/a" "$SIM_A"      > "$WORK/a.fifo" &
PA=$!
run "$WORK/b" "$SIM_B" "$@" > "$WORK/b.fifo" &
PB=$!

RC=0
if cmp "$WORK/a.fifo" "$WORK/b.fifo"; then
  wait $PA $PB
  sleep 1
  echo "books: identical, $(tr -d ' ' < "$WORK/a.lines") lines, top $LEVELS levels"
else
  echo "books: DIFFER (cmp reports the first differing line above)"
  RC=1
  wait $PA $PB
fi

# Fills, as SOM logged them: same orders, same prices, same sizes.
fills() { LC_ALL=C grep -a -h "Fill id" "$1"/sim_log.*.log 2>/dev/null | LC_ALL=C sed 's/^.*Fill id/Fill id/'; }
if diff <(fills "$WORK/a") <(fills "$WORK/b") > "$WORK/fills.diff"; then
  echo "fills: identical, $(fills "$WORK/a" | wc -l | tr -d ' ') fills"
else
  echo "fills: DIFFER, see $WORK/fills.diff"; RC=1
fi
for r in a b; do
  echo "$r: $(LC_ALL=C grep -a -h "TOT PNL" "$WORK/$r/out.txt" | tail -1 | cut -c1-160)"
  ls "$WORK/$r"/assertion__* "$WORK/$r"/sig__* 2>/dev/null
done
exit $RC
