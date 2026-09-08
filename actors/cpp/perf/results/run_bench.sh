#!/bin/bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Reproduce the runs behind ../BENCH_RESULTS_HFT_SERVER.md.
#
#   ./run_bench.sh [outdir]        # outdir defaults to this script's directory
#
# Writes {pool_on,pool_off,fastsend}_{1,2,3}.txt, then aggregate.py reduces them.
#
# Build FIRST, and build clean -- `ar rcs` never removes members, so an archive
# that has ever been built by a differently-named toolchain stays poisoned:
#
#   make -C ../..  clean && make -C ../.. && make -C ..
#
# See section 5 of the results doc for what happens if you skip the `clean`.

set -u

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
OUT=${1:-$HERE}
PERF=$(cd "$HERE/.." && pwd)

# Measured samples, warmup samples, and the cores to pin to.
N=${N:-5000000}
W=${W:-50000}
CORES=${CORES:-2,3}
RUNS=${RUNS:-3}

B="$PERF/bench_pingpong"
BN="$PERF/bench_pingpong_nopool"

for b in "$B" "$BN"; do
  [ -x "$b" ] || { echo "missing $b -- build first (see header)" >&2; exit 2; }
done

# Two PHYSICAL cores on one NUMA node, not SMT siblings: the bench spawns real
# threads (PongActor, DriverActor, bench_group), so confining producer and
# consumer to a single core measures context-switch cost, not transport cost.
# Check your topology before changing CORES:  lscpu -p=CPU,CORE,NODE
echo "N=$N warmup=$W cores=$CORES runs=$RUNS -> $OUT"
mkdir -p "$OUT"

rc=0
for i in $(seq 1 "$RUNS"); do
  taskset -c "$CORES" "$B"  "$N" "$W" all      > "$OUT/pool_on_$i.txt"   2>&1
  echo "pool_on_$i exit=$?"
  taskset -c "$CORES" "$BN" "$N" "$W" all      > "$OUT/pool_off_$i.txt"  2>&1
  echo "pool_off_$i exit=$?"
  taskset -c "$CORES" "$B"  "$N" "$W" fastsend > "$OUT/fastsend_$i.txt"  2>&1
  echo "fastsend_$i exit=$?"
done

# aggregate.py exits non-zero if fast_send < grouped < ungrouped does not hold.
python3 "$HERE/aggregate.py" "$OUT" || rc=$?
exit $rc
