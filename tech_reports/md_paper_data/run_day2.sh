#!/bin/bash
# Day-2 run matrix: book hand-off on spinning paths, feed A only, unpinned.
#
#   base_A             send to MsgBuf (sleeps),   send to TachBook (sleeps)    reference
#   mbspin_A           send to MsgBuf (spins),    send to TachBook (sleeps)
#   mbspin_tbspin_A    send to MsgBuf (spins),    send to TachBook (spins)
#   fastsend_mbspin_A  send to MsgBuf (spins),    fast_send to TachBook (inline)
#   rfs_tbspin_A       fast_send into MsgBuf,     send to TachBook (spins)
#   rfs_A              fast_send into MsgBuf,     fast_send to TachBook (inline)
#
# Phases:
#   1. smoke: 120 s of each new config, checked for decoding, gaps and spinning
#   2. main: ROUNDS rounds of all six configs, WINDOW s each, until DEADLINE.
#      Odd rounds run the list forward, even rounds in reverse, so a trend in
#      market activity across the session hits every config about equally.
#
# One-hour variant (09:00-about 10:08), as used on 2026-10-02:
#   SKIP_SMOKE=1 ROUNDS=2 WINDOW=300 DEADLINE=101000 OUT=... bash run_day2.sh
# Analyse with a 30 s warm-up: SKIP_S=30 SKIPW=3 R=<OUT> bash paper_all.sh
# The live recorder (m2_kspr) is stopped first and restarted on exit, including
# Ctrl-C and errors.
#
# usage: OUT=/home/vincent/perf/mdperf/day2 bash run_day2.sh
set -u
K=/home/vincent/kaspar-hft
M=/home/vincent/m2_kspr
S=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/home/vincent/perf/mdperf/day2_$(date +%Y%m%d)}
WINDOW=${WINDOW:-720}
ROUNDS=${ROUNDS:-3}
DEADLINE=${DEADLINE:-133000}
SKIP_SMOKE=${SKIP_SMOKE:-0}
LOG=$OUT/runs.log
mkdir -p $OUT

restart_recorder() {
  echo "$(date +%T) restarting live recorder" >> $LOG
  pkill -9 -x md_perf_meter 2>/dev/null
  (cd $M && kaspr/run_local.sh -o -d) >> $LOG 2>&1
  sleep 5
  P=$(pgrep -x kaspr) && echo "$(date +%T) recorder up pid=$P onload_maps=$(grep -c onload /proc/$P/maps)" >> $LOG \
                      || echo "$(date +%T) ALERT recorder did not come back" >> $LOG
}
trap restart_recorder EXIT

onload_drops() {
  for id in $(onload_stackdump 2>/dev/null | awk -v p="$1" '$3==p {print $1}'); do
    echo "=== stack $id"; onload_stackdump lots "$id" 2>/dev/null
  done > "$2"
}

run_one() {   # tag config seconds
  local tag=$1 c=$2 secs=$3 out=$OUT/$1 P KLOG SP RP
  if [ "$(date +%H%M%S)" -ge "$DEADLINE" ]; then echo "$(date +%T) deadline reached" >> $LOG; exit 0; fi
  mkdir -p $out
  sed -i "s#perf_csv_dir .*#perf_csv_dir ${out}#" $K/kaspr/config_${c}/md_perf.ini
  ( cd $K && KHPROJ=$K OUTDIR=$out kaspr/run_probe.sh --no-restart -t $secs \
      -c ../config_${c}/md_perf.ini > $out/probe.out 2>&1 ) &
  RP=$!
  P=""; for i in $(seq 1 60); do P=$(pgrep -x md_perf_meter) && break; sleep 1; done
  sleep 3
  KLOG=$(ls -t $K/kaspr/kaspr_log.*.log | head -1)
  echo "$(date +%T) START $tag pid=$P klog=$KLOG" >> $LOG
  python3 $S/schedsample.py "$P" $out/probe.out $out/sched.csv 10 &
  SP=$!
  # kaspr log lines carry no wall time (00/00/0000), so stamp recovery
  # milestones as they appear: epoch-seconds, then the log line. Lines written
  # before this starts (~3 s after launch) get the time they were read.
  ( tail -F -n +1 "$KLOG" 2>/dev/null | grep -a --line-buffered -E "recovery done|initiating" \
      | while IFS= read -r l; do echo "$(date +%s.%N) $l"; done > $out/recovery.txt ) &
  TP=$!
  sleep $((secs + 20))
  [ -n "$P" ] && onload_drops "$P" $out/onload.txt
  sleep 10
  pgrep -x md_perf_meter >/dev/null && pkill -9 -x md_perf_meter
  wait $RP 2>/dev/null; kill $SP 2>/dev/null
  pkill -P $TP 2>/dev/null; kill $TP 2>/dev/null
  cp "$KLOG" $out/kaspr.log 2>/dev/null
  echo "$(date +%T) END $tag gaps=$(grep -acE 'have gap|waiting for gap' $out/kaspr.log) spinning_threads_in_last_sample=$(awk -F, -v t=$(tail -1 $out/sched.csv | cut -d, -f1) '$1==t && $4>9000' $out/sched.csv | wc -l)" >> $LOG
  sleep 5
}

echo "$(date +%T) day2 start OUT=$OUT WINDOW=$WINDOW ROUNDS=$ROUNDS DEADLINE=$DEADLINE" >> $LOG
(cd $M && kaspr/stop_kaspr.sh) >> $LOG 2>&1
sleep 2
if pgrep -x kaspr >/dev/null; then
  # Do not measure next to a live recorder. Leave it running: clear the trap so
  # exit does not start a second one.
  trap - EXIT
  echo "$(date +%T) ABORT recorder still running pid=$(pgrep -x kaspr)" >> $LOG
  exit 1
fi
echo "$(date +%T) recorder stopped" >> $LOG

if [ "$SKIP_SMOKE" != 1 ]; then
  for c in mbspin_tbspin_A rfs_tbspin_A rfs_A; do run_one smoke_$c $c 120; done
fi

CFGS=(base_A mbspin_A mbspin_tbspin_A fastsend_mbspin_A rfs_tbspin_A rfs_A)
n=${#CFGS[@]}
for ((r=1; r<=ROUNDS; r++)); do
  for ((k=0; k<n; k++)); do
    if (( r % 2 )); then c=${CFGS[$k]}; else c=${CFGS[$((n - 1 - k))]}; fi
    run_one d${r}_${c} $c $WINDOW
  done
done
echo "$(date +%T) schedule complete" >> $LOG
