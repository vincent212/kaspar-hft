#!/usr/bin/env python3

# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.

"""Audit hand-assigned actor message ids (Message_N<N>).

Message_N<N> ids are NOT checked for uniqueness by the compiler -- the
static_assert in Message.hpp only constrains the range [0, 512). Two types
sharing an id silently cross-dispatch. This script is the uniqueness check.

Exit status:
    0  no duplicates
    1  duplicate ids found (or a Message_N<> whose constant could not be
       resolved, which means this audit cannot vouch for it)

Usage:
    python3 setclassid/setclassid.py            # full report
    python3 setclassid/setclassid.py --quiet    # problems only (CI / make)
"""

import argparse
import os
import re
import sys
from collections import defaultdict

# Ids 512+ belong to MessageT<Derived>, which assigns them collision-free at
# runtime; only the hand-assigned range needs auditing here.
HAND_ASSIGNED_CAP = 512

# 0 and 3 are reserved by the framework and never handed out.
RESERVED = {0, 3}

# The Rust<->C++ interop ABI pins message ids on BOTH sides: the C ABI header
# (actors/rust/interop/messages/interop_messages.h) reserves 400-499, and the
# Rust dispatch tables match on the literal integers. These ids ARE the
# cross-language dispatch key, so they can never move to MessageT. Everything
# else has been migrated; a Message_N outside these files is now a red flag.
INTEROP_RANGE = range(400, 500)
INTEROP_FILES = {"actors/rust/interop/generated/cpp/InteropMessages.hpp"}

# Directories with no hand-written message types (generated codecs, build
# output, vendored toolchains). Skipping them keeps the scan fast and stops
# generated SBE headers from muddying the report.
SKIP_DIRS = {".git", ".claude", "mdp3_sbe", "ilink3_sbe", "target", "obj", "objg",
             ".cache", "build", "node_modules"}

# Only count BASE-CLASS declarations -- `struct X : public Message_N<N>`. Bare
# uses such as `static_assert(Message_N<42>::id == 42)` in the framework's own
# tests are not message types and must not be reported as collisions.
# Group 1 is the declared type name (when on the same line), group 2 the id.
MSG_N = re.compile(
    r'(?:(?:struct|class)\s+(\w+)\b[^:]*)?:\s*public\s+(?:actors::)?Message_N\s*<\s*([^>]+?)\s*>')
CONST_DEF = re.compile(r'constexpr\s+int\s+(\w+)\s*=\s*(\d+)')
DEFINE_DEF = re.compile(r'#define\s+(\w+)\s+(\d+)')


def source_files(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in sorted(filenames):
            if fn.endswith((".hpp", ".h", ".cpp")):
                yield os.path.join(dirpath, fn)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quiet", action="store_true",
                    help="print only problems (for CI / make check-msgids)")
    ap.add_argument("--root", default=None, help="repo root (default: $KSPRPROJ or this repo)")
    args = ap.parse_args()

    root = args.root or os.environ.get("KSPRPROJ") or \
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    root = os.path.abspath(root)

    files = list(source_files(root))

    # Pass 1 -- resolve `constexpr int MSG_X = N;` / `#define MSG_X N` so that
    # Message_N<MSG_X> can be reported by value.
    constants = {}
    for f in files:
        try:
            with open(f, "r", errors="replace") as fd:
                for line in fd:
                    for rx in (CONST_DEF, DEFINE_DEF):
                        m = rx.search(line)
                        if m:
                            constants[m.group(1)] = int(m.group(2))
        except OSError:
            continue

    # Pass 2 -- collect every Message_N<> site.
    by_id = defaultdict(list)      # id -> [(file, line, raw)]
    unresolved = []                # (file, line, raw)
    for f in files:
        rel = os.path.relpath(f, root)
        try:
            with open(f, "r", errors="replace") as fd:
                lines = fd.readlines()
        except OSError:
            continue
        for n, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith(("//", "*", "/*")):
                continue
            m = MSG_N.search(line)
            if not m:
                continue
            tname = m.group(1) or "?"
            raw = m.group(2).strip()
            if raw == "N":          # the template definition itself
                continue
            try:
                val = int(raw)
            except ValueError:
                if raw in constants:
                    val = constants[raw]
                else:
                    unresolved.append((rel, n, raw))
                    continue
            by_id[val].append((rel, n, raw, tname))

    duplicates = {
        i: sites for i, sites in by_id.items()
        if len({t for *_, t in sites if t != "?"}) > 1
        or (len(sites) > 1 and any(t == "?" for *_, t in sites))
    }
    out_of_range = {i: sites for i, sites in by_id.items() if i >= HAND_ASSIGNED_CAP}

    used = set(by_id)
    free = [i for i in range(HAND_ASSIGNED_CAP) if i not in used and i not in RESERVED]
    def _interop(i):
        return any(rel in INTEROP_FILES for rel, *_ in by_id[i])
    interop_used = sorted(i for i in used if _interop(i))
    migratable = sorted(i for i in used if not _interop(i))

    if not args.quiet:
        print(f"root: {root}")
        print(f"scanned {len(files)} source files\n")
        for i in sorted(by_id):
            tag = ("  [interop: pinned]"
                   if any(rel in INTEROP_FILES for rel, *_ in by_id[i]) else "")
            for rel, n, raw, tname in by_id[i]:
                shown = f"{raw} -> {i}" if raw != str(i) else str(i)
                print(f"  {i:>3}  {tname:<28} {rel}:{n}  ({shown}){tag}")
        print("\n=== MESSAGE ID ALLOCATION ===")
        print(f"hand-assigned ids in use : {len(used)}/{HAND_ASSIGNED_CAP}")
        print(f"  interop-pinned (400-499): {len(interop_used)}  {interop_used}")
        print(f"  migratable to MessageT  : {len(migratable)}")
        print(f"free slots               : {len(free)}")
        print(f"reserved (never issued)  : {sorted(RESERVED)}")

    status = 0
    if duplicates:
        status = 1
        print("\n*** DUPLICATE MESSAGE IDS ***", file=sys.stderr)
        for i in sorted(duplicates):
            names = sorted({t for *_, t in duplicates[i]})
            print(f"  id {i} used by different types {names}:", file=sys.stderr)
            for rel, n, raw, tname in duplicates[i]:
                print(f"      {tname:<28} {rel}:{n}", file=sys.stderr)
        print(f"\n  Free slots: {free[:20]}{' ...' if len(free) > 20 else ''}", file=sys.stderr)

    if out_of_range:
        status = 1
        print("\n*** ID >= 512 (reserved for MessageT) ***", file=sys.stderr)
        for i in sorted(out_of_range):
            for rel, n, _, _t in out_of_range[i]:
                print(f"  id {i}  {rel}:{n}", file=sys.stderr)

    if unresolved:
        status = 1
        print("\n*** UNRESOLVED Message_N<> CONSTANT ***", file=sys.stderr)
        print("  (this audit cannot prove these are unique)", file=sys.stderr)
        for rel, n, raw in unresolved:
            print(f"  {rel}:{n}  Message_N<{raw}>", file=sys.stderr)

    if status == 0 and not args.quiet:
        print("\nOK: no duplicate message ids.")
    return status


if __name__ == "__main__":
    sys.exit(main())
