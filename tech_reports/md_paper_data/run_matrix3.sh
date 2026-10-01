#!/bin/bash
# Takes over from run_matrix2.sh: finishes p2_p4s (started 10:50:37, 480 s),
# completes pass 2, then runs the pinning / reader-fast_send experiments
# interleaved with reference runs, until DEADLINE.
# Pinned configs ("*_pin*") run under a CPU mask that keeps every unpinned
# kaspr thread off NUMA node 2, where the hot threads are pinned.
set -u
DEADLINE=${DEADLINE:-130000}
K=/home/vincent/kaspar-hft
S=/tmp/claude-1010/-home-vincent-m2-kspr/3220c8e8-7120-4ad4-9718-69a60284736a/scratchpad
OUTBASE=/home/vincent/perf/mdperf/paper
LOG=$OUTBASE/runs.log
NOT_NODE2=0-15,24-47,56-63

onload_drops() {   # $1 pid, $2 outfile
  for id in $(onload_stackdump 2>/dev/null | awk -v p="$1" '$3==p {print $1}'); do
    echo "=== stack $id"; onload_stackdump lots "$id" 2>/dev/null
  done > "$2"
}

# 1. finish the in-flight p2_p4s
until [ "$(date +%H%M%S)" -ge 105907 ]; do sleep 5; done
P=$(pgrep -x md_perf_meter); [ -n "$P" ] && onload_drops "$P" $OUTBASE/p2_p4s/onload.txt
pkill -9 -x md_perf_meter; pkill -f "[s]chedsample.py"
cp /home/vincent/kaspar-hft/kaspr/kaspr_log.20261001.53433307277000.2762222.log $OUTBASE/p2_p4s/kaspr.log
echo "$(date +%T) END p2_p4s (finished by run_matrix3)" >> $LOG
sleep 5

SCHED=(
  "p2_mbspin_A:mbspin_A" "p2_fastsend_mbspin:fastsend_mbspin"
  "x1_base:base" "x1_fastsend_mbspin:fastsend_mbspin" "x1_fsmb_pin:fsmb_pin"
  "x1_rfs:rfs" "x1_rfs_pin:rfs_pin" "x1_rfs_pin_A:rfs_pin_A"
  "x2_rfs_pin:rfs_pin" "x2_fsmb_pin:fsmb_pin" "x2_rfs_pin_A:rfs_pin_A"
  "x2_fastsend_mbspin:fastsend_mbspin" "x2_rfs:rfs" "x2_base:base"
)
secs=480
for item in "${SCHED[@]}"; do
  IFS=: read -r tag c <<< "$item"
  if [ "$(date +%H%M%S)" -ge "$DEADLINE" ]; then
    echo "$(date +%T) deadline reached" >> $LOG; exit 0
  fi
  out=$OUTBASE/$tag
  mkdir -p $out
  sed -i "s#perf_csv_dir .*#perf_csv_dir ${out}#" $K/kaspr/config_${c}/md_perf.ini
  MASK=()
  case "$c" in *_pin*) MASK=(taskset -c $NOT_NODE2) ;; esac
  ( cd $K && KHPROJ=$K OUTDIR=$out "${MASK[@]}" kaspr/run_probe.sh --no-restart -t $secs \
      -c ../config_${c}/md_perf.ini > $out/probe.out 2>&1 ) &
  RP=$!
  P=""
  for i in $(seq 1 60); do P=$(pgrep -x md_perf_meter) && break; sleep 1; done
  sleep 3
  KLOG=$(ls -t $K/kaspr/kaspr_log.*.log | head -1)
  echo "$(date +%T) START $tag pid=$P mask=${MASK[*]:-none} klog=$KLOG" >> $LOG
  python3 $S/schedsample.py "$P" $out/probe.out $out/sched.csv 10 &
  SP=$!
  sleep $((secs + 20))
  [ -n "$P" ] && onload_drops "$P" $out/onload.txt
  sleep 10
  if pgrep -x md_perf_meter >/dev/null; then pkill -9 -x md_perf_meter; fi
  wait $RP 2>/dev/null
  kill $SP 2>/dev/null
  cp "$KLOG" $out/kaspr.log 2>/dev/null
  echo "$(date +%T) END $tag" >> $LOG
  sleep 5
done
echo "$(date +%T) schedule complete" >> $LOG
