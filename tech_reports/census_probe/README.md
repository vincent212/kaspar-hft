# Packet-purity census probe — patch, not code

`census_probe.patch` applies to `mdp3/include/mdp3/DataDecoder.hpp` on main
(`4a191cd` blob). It is the instrumentation that produced every census figure
quoted in `../parallel_decode_status.md`:

- **3.03% of packets** are mixed hot+cold (10.21% of messages)
- the cold template is `MDIncrementalRefreshVolume37` in **1,419 of 1,419**
  mixed packets
- sample: live ES chan 310, 80,000 packets

## Why it is a patch and not a commit

The probe **disables the mixed-packet `ASSERTF` at `DataDecoder.hpp:701`**.
That assert is the only thing stopping a parallel-decode run from silently
forking the `orderid_to_securityid` map. With it off, **the books it produces
are wrong**. It is a counting instrument, never a build you trade from.

Committing it as code would put a disabled safety assert on a branch where
someone could merge it. Keeping it as a patch means it has to be applied
deliberately.

## Applying it

```bash
git apply tech_reports/census_probe/census_probe.patch
```

It adds `uint16_t cold_tid` to `scan_result`, per-template census counters,
and a `census_report()` that prints every 20,000 packets. Revert with
`git checkout -- mdp3/include/mdp3/DataDecoder.hpp` when done.

## Status

Parallel decode is shelved (see `../parallel_decode_status.md`). This probe is
kept only so the census numbers in that document are reproducible, not because
any further measurement is planned.
