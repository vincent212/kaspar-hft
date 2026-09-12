#!/bin/bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Build per-date universes for a channel across all dates, in parallel, then
# merge them into master_universe.<chan>.csv. See build_universe_day.sh for why
# each IR window needs its own decode context.
#
# Usage: ./build_universe_all.sh <chan> [DATE ...]      (no dates = all in $SRC)
#   NJOBS=N  SRC=/path/to/captures

set -u
cd "$(dirname "$0")" || exit 1
export KSPRPROJ=${KSPRPROJ:-$(cd ../.. && pwd)}

CHAN=${1:?usage: build_universe_all.sh <chan> [DATE ...]}; shift
SRC=${SRC:-/nvs/vendor/databento/pcaps/glbx/futures-xcme}
NJOBS=${NJOBS:-$(nproc 2>/dev/null || echo 16)}
OUT=$PWD/out/universe/$CHAN
export SRC KSPRPROJ

if [ $# -gt 0 ]; then DATES=("$@"); else mapfile -t DATES < <(ls -1 "$SRC" | grep -E '^20[0-9]{6}$' | sort); fi

echo "universe: chan=$CHAN dates=${#DATES[@]} NJOBS=$NJOBS"
printf '%s\n' "${DATES[@]}" | xargs -P "$NJOBS" -I{} nice -n 19 ./build_universe_day.sh "$CHAN" {}

# Merge: union across dates, unique by securityID. Definitions are stable
# across adjacent dates, so a session with a thin IR capture still resolves.
MASTER=$OUT/master_universe.$CHAN.csv
{
    echo "type,securityID,symbol,venue,asset,minPriceIncrement,minCabPrice,dispFactor,tickSize,securityType"
    cat "$OUT"/universe."$CHAN".*.csv 2>/dev/null | grep -v "^type," | sort -t, -k2,2n -u
} > "$MASTER"
echo "merged: $(($(wc -l < "$MASTER") - 1)) instruments ($(awk -F, '$1=="FDF"' "$MASTER" | wc -l) FDF) -> $MASTER"
