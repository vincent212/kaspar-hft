import re, glob
COLS=["p50","p90","p99","p99.9","min","max","mean","amort"]
def parse(path):
    out={}
    for line in open(path):
        m=re.match(r"^(\S.*?)\s+((?:[-\d.]+\s+){7}[-\d.]+)\s*$", line.rstrip())
        if not m: continue
        n=m.group(1).strip()
        if n.startswith("#") or n=="mode": continue
        v=[float(x) for x in m.group(2).split()]
        if len(v)==8: out[n]=v
    return out
files=sorted(glob.glob("/home/vincent/kh-vm/actors/cpp/perf/results/linux_quiet/all_*.txt"))
runs=[parse(f) for f in files]
Q=["BQueue","BQueueBatched","ShardedBQueue","LockFreeMPSC"]
for sec,col in [("solo1","p50"),("burst16","amort"),("fanin","p50"),("fanin","p99"),
                ("fanin","p99.9"),("fanin","amort")]:
    ci=COLS.index(col)
    print(f"\n--- {sec} {col} per run (ranking stability) ---")
    for q in Q:
        k=f"{sec} {q}"
        vals=[r[k][ci] for r in runs if k in r]
        print(f"  {q:16s} " + " ".join(f"{v:10.1f}" for v in vals) +
              f"   spread {max(vals)/min(vals):.2f}x")
    # winner per run
    wins=[]
    for r in runs:
        best=min(Q, key=lambda q: r[f"{sec} {q}"][ci])
        wins.append(best)
    print(f"  winner per run: {wins}  -> {'STABLE' if len(set(wins))==1 else 'UNSTABLE'}")
