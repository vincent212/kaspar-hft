#!/usr/bin/env bash
#
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.
#
# compare_inner.sh -- how close the inner levels of two sim builds' books are.
#
#   compare_inner.sh <sim A> <sim B> <capture.bin> <universe.json> <contract> \
#                    <t0 epoch ns> <t1 epoch ns> <probe date YYYYMMDD> [extra B args...]
#
# Same runs as compare_books.sh, but for a B that is expected to differ: a
# small ring ladder (e.g. --ob-ladder 64) drops orders below its window, so its
# book is close to A's near the touch and not identical. Each run's OB debug
# stream is reduced to the inner 4 levels (price and size) after every print,
# and inner_levels.py aligns the two by transactTime and reports how often
# they agree. Fills and SOM summaries are compared too.
#
set -uo pipefail

[ $# -ge 8 ] || { sed -n '7,17p' "$0"; exit 2; }
SIM_A=$(realpath "$1"); SIM_B=$(realpath "$2"); CAP=$(realpath "$3")
UNI=$(realpath "$4"); CONTRACT=$5; T0=$6; T1=$7; DAY=$8
shift 8
HERE=$(cd "$(dirname "$0")" && pwd)
CONFIG=${CONFIG:-$HERE/../../sim/config_a}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/obinner.XXXXXX")
mkdir -p "$WORK/a" "$WORK/b"
echo "work dir: $WORK"

# One line per book print: "t B:0:px:orders:contracts B:1:... A:0:...". OB prints
# a level as [px:orders:contracts].
inner() {
  LC_ALL=C grep -a -E '^(sym=|[AB]:[0-3] )| ring [0-9]+: dropped' |
    LC_ALL=C awk '
      function flush() { if (t != "") print t lv; lv = "" }
      / ring [0-9]+: dropped/ { print > "ring.txt"; next }
      /^sym=/ { flush(); for (i = 1; i <= NF; i++) if ($i ~ /^t=/) t = substr($i, 3); next }
      { split($1, s, ":"); split(substr($2, 2), p, ":"); lv = lv " " s[1] ":" s[2] ":" p[1] ":" p[2] ":" (p[3] + 0) }
      END { flush() }'
}

run() {   # dir sim extra...
  local dir=$1 sim=$2; shift 2
  cd "$dir" && "$sim" --datafile "$CAP" --universe "$UNI" --contract "$CONTRACT" \
      --config "$CONFIG" --probe-size 10 --probe-date "$DAY" \
      --ob-debug "$T0" --end-ts "$T1" "$@" 2>&1 >out.txt | inner > ../$(basename "$dir").books
}

run "$WORK/a" "$SIM_A" &
PA=$!
run "$WORK/b" "$SIM_B" "$@" &
PB=$!
wait $PA $PB

python3 "$HERE/inner_levels.py" "$WORK/a.books" "$WORK/b.books"

fills() { LC_ALL=C grep -a -h "Fill id" "$1"/sim_log.*.log 2>/dev/null | LC_ALL=C sed 's/^.*Fill id/Fill id/'; }
echo "fills: A $(fills "$WORK/a" | wc -l | tr -d ' '), B $(fills "$WORK/b" | wc -l | tr -d ' '), \
lines that differ $(diff <(fills "$WORK/a") <(fills "$WORK/b") | grep -c '^[<>]')"
for r in a b; do
  echo "$r: $(LC_ALL=C grep -a -h "TOT PNL" "$WORK/$r/out.txt" | tail -1 | cut -c1-120)"
  cat "$WORK/$r/ring.txt" 2>/dev/null
  ls "$WORK/$r"/assertion__* "$WORK/$r"/sig__* 2>/dev/null
done
