import re, glob, statistics as st
COLS=["p50","p90","p99","p99.9","min","max","mean","amort"]
D="/home/vincent/kh-vm/actors/cpp/perf/results/linux_quiet/"
def parse(p):
    out={}
    for line in open(p):
        m=re.match(r"^(\S.*?)\s+((?:[-\d.]+\s+){7}[-\d.]+)\s*$", line.rstrip())
        if not m: continue
        n=m.group(1).strip()
        if n.startswith("#") or n=="mode": continue
        v=[float(x) for x in m.group(2).split()]
        if len(v)==8: out[n]=v
    return out
Q=["BQueue","BQueueBatched","ShardedBQueue","LockFreeMPSC"]
print("solo p50 (ns), window=1 cross-thread, by thread placement")
print(f"{'queue':16s} {'sameCCD(2,3)':>26s} {'diffCCD(2,6)':>26s} {'diffNUMA(2,26)':>26s}")
res={}
for tag in ["sameccd","diffccd","diffnuma"]:
    runs=[parse(f) for f in sorted(glob.glob(D+f"solo_{tag}_*.txt"))]
    res[tag]={q: [r[f"solo1 {q}"][0] for r in runs if f"solo1 {q}" in r] for q in Q}
for q in Q:
    cells=[]
    for tag in ["sameccd","diffccd","diffnuma"]:
        v=res[tag][q]
        cells.append(f"{st.median(v):7.0f} [{min(v):.0f}-{max(v):.0f}]")
    print(f"{q:16s} " + " ".join(f"{c:>26s}" for c in cells))
print()
for tag in ["sameccd","diffccd","diffnuma"]:
    allv=[x for q in Q for x in res[tag][q]]
    print(f"{tag:10s} all-queue median {st.median(allv):7.0f}  min {min(allv):6.0f}  max {max(allv):6.0f}  spread {max(allv)/min(allv):.2f}x")
print()
for tag in ["sameccd","diffccd","diffnuma"]:
    wins=[]
    runs=range(len(res[tag]["BQueue"]))
    for i in runs:
        wins.append(min(Q, key=lambda q: res[tag][q][i]))
    print(f"{tag:10s} winner per rep: {wins}  -> {'STABLE' if len(set(wins))==1 else 'UNSTABLE'}")
