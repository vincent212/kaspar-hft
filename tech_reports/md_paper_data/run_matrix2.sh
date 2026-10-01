#!/bin/bash
# Takes over from run_matrix.sh: finishes the in-flight run (p1_fastsend), then
# runs an explicit schedule until DEADLINE. "_A" configs = feed B off.
set -u
DEADLINE=${DEADLINE:-121000}
K=/home/vincent/kaspar-hft
S=/tmp/claude-1010/-home-vincent-m2-kspr/3220c8e8-7120-4ad4-9718-69a60284736a/scratchpad
OUTBASE=/home/vincent/perf/mdperf/paper
LOG=$OUTBASE/runs.log

# 1. finish the in-flight run: started 09:07:23 with a 600 s window
until [ "$(date +%H%M%S)" -ge 091753 ]; do sleep 5; done
pkill -9 -x md_perf_meter
pkill -f "[s]chedsample.py"
cp /home/vincent/kaspar-hft/kaspr/kaspr_log.20261001.47239184150000.2613213.log $OUTBASE/p1_fastsend/kaspr.log
echo "$(date +%T) END p1_fastsend (finished by run_matrix2)" >> $LOG
sleep 5

SCHED=(
  "p1_p4s:p4s:600" "p1_mbspin:mbspin:600" "p1_fastsend_mbspin:fastsend_mbspin:600"
  "p2_base_A:base_A:480" "p2_fastsend:fastsend:480" "p2_p4s_A:p4s_A:480" "p2_mbspin:mbspin:480"
  "p2_fastsend_mbspin_A:fastsend_mbspin_A:480" "p2_base:base:480" "p2_fastsend_A:fastsend_A:480"
  "p2_p4s:p4s:480" "p2_mbspin_A:mbspin_A:480" "p2_fastsend_mbspin:fastsend_mbspin:480"
  "p3_fastsend_mbspin:fastsend_mbspin:480" "p3_mbspin_A:mbspin_A:480" "p3_p4s:p4s:480"
  "p3_fastsend_A:fastsend_A:480" "p3_base:base:480" "p3_fastsend_mbspin_A:fastsend_mbspin_A:480"
  "p3_mbspin:mbspin:480" "p3_p4s_A:p4s_A:480" "p3_fastsend:fastsend:480" "p3_base_A:base_A:480"
)

for item in "${SCHED[@]}"; do
  IFS=: read -r tag c secs <<< "$item"
  if [ "$(date +%H%M%S)" -ge "$DEADLINE" ]; then
    echo "$(date +%T) deadline reached" >> $LOG; exit 0
  fi
  out=$OUTBASE/$tag
  mkdir -p $out
  sed -i "s#perf_csv_dir .*#perf_csv_dir ${out}#" $K/kaspr/config_${c}/md_perf.ini
  ( cd $K && KHPROJ=$K OUTDIR=$out kaspr/run_probe.sh --no-restart -t $secs \
      -c ../config_${c}/md_perf.ini > $out/probe.out 2>&1 ) &
  RP=$!
  P=""
  for i in $(seq 1 60); do P=$(pgrep -x md_perf_meter) && break; sleep 1; done
  sleep 3
  KLOG=$(ls -t $K/kaspr/kaspr_log.*.log | head -1)
  echo "$(date +%T) START $tag pid=$P klog=$KLOG" >> $LOG
  python3 $S/schedsample.py "$P" $out/probe.out $out/sched.csv 10 &
  SP=$!
  sleep $((secs + 30))
  if pgrep -x md_perf_meter >/dev/null; then pkill -9 -x md_perf_meter; fi
  wait $RP 2>/dev/null
  kill $SP 2>/dev/null
  cp "$KLOG" $out/kaspr.log 2>/dev/null
  echo "$(date +%T) END $tag" >> $LOG
  sleep 5
done
echo "$(date +%T) schedule complete" >> $LOG
