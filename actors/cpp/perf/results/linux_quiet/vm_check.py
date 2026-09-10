import glob, re, os
D = "/home/vincent/kh-vm/actors/cpp/perf/results/linux_quiet"
COLS = ["p50","p90","p99","p999","min","max","mean","amort"]

def rows(path):
    out = {}
    for line in open(path):
        m = re.match(r"^(\S+(?: \S+)?)\s+((?:\d+(?:\.\d+)?\s+){7}\d+(?:\.\d+)?)\s*$", line.rstrip())
        if not m: continue
        out[m.group(1)] = dict(zip(COLS, [float(x) for x in m.group(2).split()]))
    return out

print("=== solo1 p50 per unpinned run ===")
runs = [rows(os.path.join(D, f"all_{i}.txt")) for i in (1,2,3)]
for q in ["solo1 BQueue","solo1 BQueueBatched","solo1 ShardedBQueue","solo1 LockFreeMPSC"]:
    print(f"{q:26s}", " ".join(f"{r[q]['p50']:8.0f}" for r in runs))

print("\n=== fanin per unpinned run: p999 / max ===")
for q in ["fanin BQueue","fanin BQueueBatched","fanin ShardedBQueue","fanin LockFreeMPSC"]:
    print(f"{q:26s}", " ".join(f"p999={r[q]['p999']:8.0f} max={r[q]['max']:9.0f}" for r in runs))

print("\n=== grouped dedicated: p50 / p99 / max ===")
gr = [rows(os.path.join(D, f"grouped_{i}.txt")) for i in (1,2,3)]
for q in ["grp BQueue","grp BQueueBatched","grp ShardedBQueue","grp LockFreeMPSC"]:
    print(f"{q:26s}", " ".join(f"p50={r[q]['p50']:6.0f} p99={r[q]['p99']:6.0f} max={r[q]['max']:8.0f}" for r in gr))
