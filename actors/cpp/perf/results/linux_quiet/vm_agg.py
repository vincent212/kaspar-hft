import re, glob, statistics as st

COLS = ["p50","p90","p99","p99.9","min","max","mean","amort"]

def parse(path):
    out={}
    for line in open(path):
        line=line.rstrip("\n")
        m=re.match(r"^(\S.*?)\s+((?:[-\d.]+\s+){7}[-\d.]+)\s*$", line)
        if not m: continue
        name=m.group(1).strip()
        if name.startswith("#") or name=="mode": continue
        vals=[float(x) for x in m.group(2).split()]
        if len(vals)!=8: continue
        out[name]=vals
    return out

def agg(pattern):
    runs=[parse(p) for p in sorted(glob.glob(pattern))]
    runs=[r for r in runs if r]
    keys=[]
    for r in runs:
        for k in r:
            if k not in keys: keys.append(k)
    res={}
    for k in keys:
        vs=[r[k] for r in runs if k in r]
        if not vs: continue
        res[k]=([st.median([v[i] for v in vs]) for i in range(8)], len(vs))
    return res

def show(title, res, order=None):
    print(f"\n=== {title}  (median of n runs) ===")
    print(f"{'mode':24s}" + "".join(f"{c:>10s}" for c in COLS) + "   n")
    ks = order if order else list(res)
    for k in ks:
        if k not in res: continue
        v,n=res[k]
        print(f"{k:24s}" + "".join(f"{x:10.1f}" for x in v) + f"   {n}")

A=agg("/home/vincent/kh-vm/actors/cpp/perf/results/linux_quiet/all_*.txt")
G=agg("/home/vincent/kh-vm/actors/cpp/perf/results/linux_quiet/grouped_*.txt")

Q=["BQueue","BQueueBatched","ShardedBQueue","LockFreeMPSC"]
show("SOLO  window=1 cross-thread", A, [f"solo1 {q}" for q in Q])
show("BURST16", A, [f"burst16 {q}" for q in Q])
show("GROUPED  (from 'all' run, 500k)", A, [f"grp {q}" for q in Q])
show("GROUPED  (dedicated run, 300k)", G, [f"grp {q}" for q in Q])
show("FANIN  32 producers", A, [f"fanin {q}" for q in Q])
show("TRANSPORT", A, ["send ungrouped","send grouped","fast_send","direct call (base)"])
