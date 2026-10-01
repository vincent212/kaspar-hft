#!/bin/bash
# Paper run matrix. Passes over five configs, order rotated each pass, until
# DEADLINE (HHMMSS). Each run: run_probe.sh solo + onload, RUN_SECS window, a
# scheduler sampler on the probe pid; the probe is force-killed 30 s after the
# window (known ZMQ shutdown hang).
set -u
RUN_SECS=${RUN_SECS:-600}
DEADLINE=${DEADLINE:-123500}
K=/home/vincent/kaspar-hft
S=/tmp/claude-1010/-home-vincent-m2-kspr/3220c8e8-7120-4ad4-9718-69a60284736a/scratchpad
OUTBASE=/home/vincent/perf/mdperf/paper
LOG=$OUTBASE/runs.log
mkdir -p $OUTBASE
CFGS=(base fastsend p4s mbspin fastsend_mbspin)
pass=0
echo "$(date +%T) matrix start RUN_SECS=$RUN_SECS DEADLINE=$DEADLINE" >> $LOG
while :; do
  pass=$((pass+1))
  n=${#CFGS[@]}
  for ((k=0; k<n; k++)); do
    c=${CFGS[$(( (k + pass - 1) % n ))]}
    if [ "$(date +%H%M%S)" -ge "$DEADLINE" ]; then
      echo "$(date +%T) deadline reached" >> $LOG; exit 0
    fi
    tag="p${pass}_${c}"
    out=$OUTBASE/$tag
    mkdir -p $out
    sed -i "s#perf_csv_dir .*#perf_csv_dir ${out}#" $K/kaspr/config_${c}/md_perf.ini
    ( cd $K && KHPROJ=$K OUTDIR=$out kaspr/run_probe.sh --no-restart -t $RUN_SECS \
        -c ../config_${c}/md_perf.ini > $out/probe.out 2>&1 ) &
    RP=$!
    for i in $(seq 1 60); do P=$(pgrep -x md_perf_meter) && break; sleep 1; done
    sleep 3
    KLOG=$(ls -t $K/kaspr/kaspr_log.*.log | head -1)
    echo "$(date +%T) START $tag pid=$P klog=$KLOG" >> $LOG
    python3 $S/schedsample.py "$P" $out/probe.out $out/sched.csv 10 &
    SP=$!
    sleep $((RUN_SECS + 30))
    if pgrep -x md_perf_meter >/dev/null; then pkill -9 -x md_perf_meter; fi
    wait $RP 2>/dev/null
    kill $SP 2>/dev/null
    cp "$KLOG" $out/kaspr.log 2>/dev/null
    echo "$(date +%T) END $tag" >> $LOG
    sleep 5
  done
done
