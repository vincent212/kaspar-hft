#!/bin/bash
# Regenerate every paper table from raw run directories.
#   R = raw run root (one subdirectory per run: p1_base, x2_rfs_pin, ...)
#   D = output directory for the .md tables (default: this script's directory)
#   usage: R=/path/to/runs bash paper_all.sh
S=$(cd "$(dirname "$0")" && pwd)
D=${D:-$S}
R=${R:-/home/vincent/perf/mdperf/paper}
stamp="Generated $(date '+%Y-%m-%d %H:%M %Z') from $R"
{ echo "<!-- $stamp -->"; python3 $S/paper_e2e.py $R 120; } > $D/e2e.md
{ echo "<!-- $stamp -->"; python3 $S/paper_stages.py $R 12; } > $D/stages.md
{ echo "<!-- $stamp -->"; python3 $S/paper_sched.py $R 120; } > $D/sched.md
{ echo "<!-- $stamp -->"; python3 $S/paper_health.py $R; python3 $S/paper_onload.py $R; } > $D/health.md
[ -f $R/runs.log ] && cp $R/runs.log $D/runs.log
echo "$stamp"
