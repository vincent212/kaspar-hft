#!/usr/bin/env python3
#
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.
#
# inner_levels.py -- how close two books are, transaction by transaction.
#
#   inner_levels.py <books A> <books B>
#
# Each input is the output of compare_inner.sh's extractor: one line per book
# print, "t side:d:px:orders:contracts ...", d being ticks from the touch. A
# book that legitimately differs from A -- B on a small ring ladder, say -- has a
# different number of prints, so the two are aligned by transactTime, using
# each run's last book of every transaction both runs printed.
#
import sys
from collections import defaultdict


def load(path):
    books = {}
    with open(path) as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            lv = {}
            for p in parts[1:]:
                side, d, px, orders, contracts = p.split(':')
                lv[(side, int(d))] = (int(px), int(contracts), int(orders))
            books[int(parts[0])] = lv   # the last print of a transaction wins
    return books


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    common = sorted(set(a) & set(b))
    n = len(common)
    if n == 0:
        sys.exit('no transaction in common')

    keys = [(s, d) for s in 'BA' for d in range(4)]
    same_bbo = same_all = 0
    px_same = defaultdict(int)
    sz_same = defaultdict(int)
    present = defaultdict(int)
    ord_same = defaultdict(int)
    sz_diff = defaultdict(list)   # contracts B - A where the price matches and the contracts do not
    for t in common:
        x, y = a[t], b[t]
        if x.get(('B', 0)) == y.get(('B', 0)) and x.get(('A', 0)) == y.get(('A', 0)):
            same_bbo += 1
        if all(x.get(k) == y.get(k) for k in keys):
            same_all += 1
        for k in keys:
            if k not in x and k not in y:
                continue
            present[k] += 1
            if x.get(k, (None,))[0] == y.get(k, (None,))[0]:
                px_same[k] += 1
                if x.get(k, (0, None))[1] == y.get(k, (0, None))[1]:
                    sz_same[k] += 1
                elif k in x and k in y:
                    sz_diff[k].append(y[k][1] - x[k][1])
                if x.get(k, (0, 0, None))[2] == y.get(k, (0, 0, None))[2] and x.get(k, (None,))[0] == y.get(k, (None,))[0]:
                    ord_same[k] += 1

    print(f'transactions: A {len(a)}, B {len(b)}, in both {n}')
    print(f'BBO (price and size) identical: {same_bbo / n:.4%}')
    print(f'inner 4 levels identical:       {same_all / n:.4%}')
    print('level   level present   price same   orders same   contracts same   contracts B-A when different (mean, min, max)')
    for k in keys:
        m = present[k]
        if not m:
            continue
        d = sz_diff[k]
        stats = f'{sum(d) / len(d):+.2f}, {min(d)}, {max(d)}' if d else '-'
        print(f'{k[0]}{k[1]}      {m:>12}   {px_same[k] / m:>9.4%}   {ord_same[k] / m:>10.4%}   {sz_same[k] / m:>13.4%}   {stats}')


if __name__ == '__main__':
    main()
