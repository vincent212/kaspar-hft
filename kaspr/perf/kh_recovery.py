"""
Parse kaspr's log for gap/recovery windows and turn them into per-symbol
exclusion intervals, to be subtracted from the latency bins.

WHY THIS IS NEEDED. When MessageProcessor declares a gap it enters recovery,
and processq() returns early for the whole duration (MessageProcessor.hpp:57-60)
so live packets just pile up in msg_q. At the end, enddatarecovery_handler
calls processq(0,0) -- but in_data_recovery is STILL true on that line and is
only cleared on the next one, so that call is a no-op. The backlog is therefore
drained by the NEXT arriving packet, using that packet's recv_ts as t0 for every
queued packet. Draining thousands of packets takes real time, so every packet
after the first in the drain is charged the elapsed drain time as leg 1: tens of
ms against a ~9 us baseline. Those samples are not measurements of anything.

(They also UNDERSTATE the true damage, because the seconds those packets
actually sat in msg_q are erased by the t0 reset. Slow-looking and still too
kind. Either way: not data.)

WHAT THE LOG CAN AND CANNOT GIVE YOU.

The logger's own timestamp field is broken -- every line reads
"00/00/0000 00:00:00.000000000". Do not key off the line prefix; it is zeros.

What IS usable is the timestamp some messages carry in the body, from
chutil::Time::now_utc(). That is the same clock as bin_key_ns, at ns
resolution. These carry one:

    MessageProcessor.hpp    have gap sn: %d, expected: %d, tim: %s
    MessageProcessor.hpp    waiting for gap to close ... tim: %s   [see below]
    RecoveryProcessor.hpp   starting data recovery tim: %s
    RecoveryProcessor.hpp   done data recovery tim: %s
    RecoveryProcessor.hpp   start instrument recovery tim: %s
    RecoveryProcessor.hpp   done instrument recovery tim: %s

These do NOT, and are counted but cannot be placed on the timeline:

    MessageProcessor.hpp    gap has closed
    MessageProcessor.hpp    initiating data recovery
    MessageProcessor.hpp    data recovery done

"waiting for gap to close" is the one that matters most: it is the true start
of contamination, the point where packets begin buffering in msg_q, and it
fires BEFORE any recovery event. It originally had no timestamp, so it could
only be counted, not placed. It was given a tim: (and raised to ERR) for
exactly that reason. Logs written by the older binary still lack it -- those
parse fine and report it as detected-but-unplaceable, which is why the
exclusion is also extended BACKWARDS by PRE_MARGIN_S.

If the count of unstamped events exceeds what the stamped ones account for,
this module says so rather than silently returning a clean answer. A parser
that cannot see an event must not report "no events".

SCOPE IS PER CHANNEL, NOT PER INSTRUMENT. There is one MessageProcessor per
channel and qseq_num is channel-wide, so a gap on any instrument stalls every
instrument on that channel. Exclusion is applied to all symbols on the channel.
"""
import re
import sys
from datetime import datetime, timezone

# Forward margin after "done data recovery": the drain plus however long the
# books take to settle. 60 s is a guess with headroom, not a measurement. Once
# a recovery lands inside a long run, measure the actual return-to-baseline
# from the per-bin leg1 mean and replace this number with the measured one.
POST_MARGIN_S = 60

# Backward margin before the first stamped event, to cover the unstamped
# "waiting for gap to close" window. maxwaitcnt is 3 packets, so this is
# microseconds in reality; 5 s is cheap insurance against clock skew between
# the two stamp sites.
PRE_MARGIN_S = 5

# chan -> symbols. From cme.ini: 310 prod_equity, 318 prod_nasdaq,
# 344 prod_treasury_futures.
CHAN_SYMS = {
    "310": ("ESZ6", "ESM6"),
    "318": ("NQZ6",),
    "344": ("ZNZ6",),
}

# "waiting for gap to close" appears in BOTH tables on purpose. As of the
# MessageProcessor change it carries its own tim: and is the earliest placeable
# marker of contamination. Logs written by the older binary have no tim: on that
# line, so the STAMPED match simply fails and it falls through to UNSTAMPED and
# is reported as detected-but-unplaceable. Order matters: STAMPED is tried
# first. Do not "clean this up" by removing it from one of them -- old logs must
# keep parsing, and new logs must use the better timestamp.
STAMPED = re.compile(
    r"(?P<chan>\d+)(?:Recovery|Message)Processor\s+"
    r"(?P<what>have gap|waiting for gap to close|"
    r"starting data recovery|done data recovery|"
    r"start instrument recovery|done instrument recovery)"
    r".*?tim:\s*(?P<ts>\d{8}-\d{2}:\d{2}:\d{2}\.\d+)")

UNSTAMPED = re.compile(
    r"(?P<chan>\d+)MessageProcessor\s+"
    r"(?P<what>waiting for gap to close|gap has closed|"
    r"initiating data recovery|data recovery done)")

OPENERS = ("have gap", "waiting for gap to close",
           "starting data recovery", "start instrument recovery")
CLOSERS = ("done data recovery", "done instrument recovery")


def _to_epoch_ns(s):
    """'20260916-14:31:47.290853000' (UTC) -> epoch ns."""
    date, rest = s.split("-", 1)
    hms, frac = rest.split(".", 1)
    frac = (frac + "000000000")[:9]
    dt = datetime.strptime(date + " " + hms, "%Y%m%d %H:%M:%S")
    dt = dt.replace(tzinfo=timezone.utc)
    return int(dt.timestamp()) * 1_000_000_000 + int(frac)


def parse(logpath):
    """Return (intervals_by_chan, counts, unplaced) without merging."""
    events = []
    counts = {}
    unstamped = {}
    with open(logpath, errors="replace") as f:
        for line in f:
            m = STAMPED.search(line)
            if m:
                events.append((_to_epoch_ns(m.group("ts")),
                               m.group("chan"), m.group("what")))
                counts[m.group("what")] = counts.get(m.group("what"), 0) + 1
                continue
            m = UNSTAMPED.search(line)
            if m:
                unstamped[m.group("what")] = unstamped.get(m.group("what"), 0) + 1

    events.sort()

    by_chan = {}
    for ts, chan, what in events:
        by_chan.setdefault(chan, []).append((ts, what))

    # Split each channel's events into EPISODES. Two events belong to the same
    # episode if they fall within POST_MARGIN_S of each other. A longer quiet
    # stretch than that means the earlier episode's exclusion window had already
    # expired before the next one opened, so they are genuinely separate and the
    # good data between them must be kept.
    #
    # The earlier version emitted ONE interval per channel, spanning first event
    # to last. On this run every event landed in the first 63 s so it made no
    # difference -- but a gap at minute 2 plus another at minute 25 would have
    # excluded the entire run while printing a perfectly plausible window. That
    # is the same failure that has already bitten twice here: a clean-looking
    # report from something that wasn't actually measuring. Do not reintroduce.
    #
    # Within an episode, recoveries nest (instrument then data) and re-enter (a
    # gap during recovery), so take the union rather than pairing opener to
    # closer. A union is conservative: it can over-exclude, never under.
    gap_ns = POST_MARGIN_S * 1_000_000_000
    intervals = {}
    for chan, evs in by_chan.items():
        episodes = []
        lo = hi = evs[0][0]
        for ts, _what in evs[1:]:
            if ts - hi > gap_ns:
                episodes.append((lo, hi))
                lo = ts
            hi = ts
        episodes.append((lo, hi))
        intervals[chan] = [(a - PRE_MARGIN_S * 1_000_000_000,
                            b + POST_MARGIN_S * 1_000_000_000)
                           for a, b in episodes]
    return intervals, counts, unstamped


def exclusions_for_symbol(logpath, sym):
    """List of (start_ns, end_ns) during which `sym` must not be measured."""
    intervals, _, _ = parse(logpath)
    out = []
    for chan, ivs in intervals.items():
        if sym in CHAN_SYMS.get(chan, ()):
            out.extend(ivs)
    return out


def report(logpath):
    intervals, counts, unstamped = parse(logpath)
    L = []
    L.append("recovery scan: %s" % logpath)
    if not counts:
        L.append("  NO stamped gap/recovery events found.")
        L.append("  Treat with suspicion: confirm this log is the one the run")
        L.append("  wrote (kaspr_log.<date>.<ns>.<pid>.log), not the stdout")
        L.append("  capture, which contains no log_inf/log_wrn output at all.")
    for k in sorted(counts):
        L.append("  %-28s %d" % (k, counts[k]))
    if unstamped:
        L.append("  unstamped (detected, not placeable):")
        for k in sorted(unstamped):
            L.append("    %-26s %d" % (k, unstamped[k]))
    L.append("  exclusion windows (pre=%ds post=%ds):" % (PRE_MARGIN_S, POST_MARGIN_S))
    for chan in sorted(intervals):
        for lo, hi in intervals[chan]:
            L.append("    chan %s -> %-18s  %s .. %s  (%.1fs)"
                     % (chan, ",".join(CHAN_SYMS.get(chan, ("?",))),
                        datetime.fromtimestamp(lo / 1e9).strftime("%H:%M:%S"),
                        datetime.fromtimestamp(hi / 1e9).strftime("%H:%M:%S"),
                        (hi - lo) / 1e9))
    return "\n".join(L)


if __name__ == "__main__":
    print(report(sys.argv[1]))
