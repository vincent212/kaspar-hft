# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Test fixtures for the kaspar_arrival module.

The core fixtures build tiny synthetic message_tape + bbbochg_tape CSVs in a
tmp_path so tests can exercise the builder end-to-end without needing any of
the real ~2 TB corpus. Each row is a single-line writer so failures point at
the exact row in the fixture.
"""

from __future__ import annotations

import gzip
import io
from pathlib import Path

import pytest


# ---------------------------------------------------------------------------
# CSV writers
# ---------------------------------------------------------------------------


MSG_HEADER = ("transactTime,sendingTime,handlerendtim,recv_time,"
              "packet_seq,idx_in_packet,"
              "typ,action,side,pxd,sz,orderID")


def msg_row(*, ts: int, typ: str, action: str = "", side: str = "",
            pxd: float = 0.0, sz: int = 0, oid: int = 0,
            packet_seq: int = 0, idx_in_packet: int = 0,
            sending: int | None = None, handler: int | None = None,
            recv_time: int | str = "") -> str:
    """Build one CSV row in the msgtape schema.

    Timestamps in nanoseconds. `sending` and `handler` default to `ts` if
    unset, so ordering-only tests don't need to spell them out.
    """
    if sending is None:
        sending = ts
    if handler is None:
        handler = ts
    pxd_s = f"{pxd:.9f}" if action != "" and typ == "M" else ""
    if typ == "M":
        pxd_s = f"{pxd:.9f}"
    return (f"{ts},{sending},{handler},{recv_time},"
            f"{packet_seq},{idx_in_packet},"
            f"{typ},{action},{side},{pxd_s},{sz},{oid}")


def write_msg_tape(path: Path, rows: list[str]) -> None:
    """Write a msgtape CSV to `path`."""
    with open(path, "w") as f:
        f.write(MSG_HEADER + "\n")
        for r in rows:
            f.write(r + "\n")


BBO_HEADER = "tx_time,venue,sym,best_bid,best_ask"


def bbo_row(*, ts: int, sym: int, bid: int, ask: int, venue: int = 1) -> str:
    """Build one CSV row in the bbbochg schema."""
    return f"{ts},{venue},{sym},{bid},{ask}"


def write_bbo_tape(path: Path, rows: list[str], gzipped: bool = True) -> None:
    """Write a bbbochg CSV, optionally gzip-compressed."""
    body = BBO_HEADER + "\n" + "\n".join(rows) + "\n"
    if gzipped:
        with gzip.open(path, "wt") as f:
            f.write(body)
    else:
        path.write_text(body)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture
def msg_writer():
    return msg_row


@pytest.fixture
def write_msg():
    return write_msg_tape


@pytest.fixture
def write_bbo():
    return write_bbo_tape


@pytest.fixture
def bbo_writer():
    return bbo_row


@pytest.fixture
def simple_bbo(tmp_path, write_bbo, bbo_writer):
    """A 21-row BBO tape with a constant 100 ticks-wide spread around a
    mid that walks from 1000 to 2000 in 10-tick steps every 1 s. Sym 10."""
    rows = []
    for i, mid in enumerate(range(1000, 2010, 50)):
        ts = 1_000_000_000 + i * 1_000_000_000
        bid = mid - 5
        ask = mid + 5
        rows.append(bbo_writer(ts=ts, sym=10, bid=bid, ask=ask))
    p = tmp_path / "bbo.csv.gz"
    write_bbo(p, rows, gzipped=True)
    return p, rows
