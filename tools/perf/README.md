# Latency-probe analysis scripts

These read `/home/vincent/perf/mdperf/lat_*.csv`, the output of
`frame_kaspr/include/frame/perf/act/LatencyProbe.hpp`. They are analysis, not
build inputs — nothing here is compiled or run by CI.

## Promoted

**`kh_sane.py`** — integrity scan. No statistics, no stratification: it only
asks whether each row obeys the arithmetic it must obey. Per population
(`book`, `trade`) it flags `n==0 && sum!=0`, `min>max`, mean outside
`[min,max]`, `all_n < l1_n`, `all_n > span_sum`, and negative batch variance.

The `ia` check is the subtle one and the comment in the file is worth reading
before changing it. `ia_sum` is exact nanoseconds; `ia_sumsq_us2` is
microseconds squared, and `LatencyProbe.hpp:1015` squares `gap/1000` as an
**integer**, so every term is floored before squaring. A naive
Cauchy-Schwarz test on those two columns reports overflows that never
happened. The script tests against the truncation lower bound
`floor(g/1000) > g/1000 - 1` instead, so it can only fire on a real wrap.
This matters most on busy book streams, where sub-10us gaps make the floored
1us quantum a large share of the gap.

**`kh_span3.py`** — follow-up on the `all_n > span_sum` violations `kh_sane`
reports. It histograms the difference. A difference of exactly 1 is a packet
straddling a bin edge: its messages land in two bins but its span is credited
to one. A broad difference means `span_sum` counts something else. Book and
trade streams separate by eye, which is why the open `all_n > span_sum`
question is still open — trade shows differences of 10+.

## Scratch

`scratch/oneshot_probes_2026-09.tar.gz` is 62 one-shot probe scripts written
during September, archived verbatim rather than committed individually. They
were each written to answer one question and are not maintained, not
documented, and in several cases hardcode paths and dates. They are kept
because a few encode measurement setups that would be tedious to rebuild, not
because they are usable as-is.

Do not treat anything in `scratch/` as a tool. Read it, take what you need,
and write something new.
