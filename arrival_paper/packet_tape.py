# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""packet_tape — one row per UDP packet observed in a message_tape.

Reads a msgtape CSV (per-securityID output of the C++ msgtape tool) and
aggregates by `packet_seq`, producing one parquet row per unique packet:

    packet_seq         first packet-seq value (also = row's packet_seq)
    transactTime_first matching-engine timestamp of the first msg in this packet
    transactTime_last  matching-engine timestamp of the last msg in this packet
    sendingTime_first  gateway timestamp on first msg
    sendingTime_last   gateway timestamp on last msg
    handlerendtim      packet's decode-completed kernel timestamp (constant)
    recv_time          non-zero if any msg in the packet carried it (else 0)
    n_msgs             count of msgs in this packet matching the msgtape's secID
    max_idx_in_packet  max idx_in_packet seen — the packet as a whole may have
                       more msgs (positions that failed the secID filter)
    n_add              MBO Add count in this packet (action = '0')
    n_modify           MBO Change count (action = '1')
    n_delete           MBO Delete count (action = '2')
    n_trade            Trade count (typ = 'T')

Grain: one row per packet. If a packet had no messages for the target secID,
the corresponding row is absent (holes are fine, msgtape already filters).

This tape is the primary input for two paper analyses:
    (a) packet-size distribution (msgs-per-packet histogram, Fano scaling)
    (b) intra-packet sequence effect — do late-idx messages in the same
        packet mark different arrival dynamics vs. early-idx messages?
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Optional

import pandas as pd


REQUIRED_COLS = ("transactTime", "sendingTime", "handlerendtim", "recv_time",
                 "packet_seq", "idx_in_packet",
                 "typ", "action")


def build_packet_tape(msg_tape_csv: str,
                      out_parquet: str,
                      chunksize: int = 2_000_000) -> pd.DataFrame:
    """Build the packet-tape parquet. Streams msg_tape in chunks to keep
    peak memory bounded on multi-GB CSVs."""
    header = pd.read_csv(msg_tape_csv, nrows=0)
    missing = [c for c in REQUIRED_COLS if c not in header.columns]
    if missing:
        raise RuntimeError(
            f"{msg_tape_csv} missing required columns: {missing}. "
            "Re-run msgtape (it must include packet_seq + idx_in_packet)."
        )

    dtypes = {
        "transactTime": "int64",
        "sendingTime": "int64",
        "handlerendtim": "int64",
        "recv_time": "float64",   # can be blank on MBO rows
        "packet_seq": "int64",
        "idx_in_packet": "int32",
        "typ": "string",
        "action": "string",
    }
    usecols = list(REQUIRED_COLS)

    # We only want the aggregated packet-level state, so build a rolling
    # aggregator instead of loading the whole file.
    agg: Optional[pd.DataFrame] = None
    reader = pd.read_csv(
        msg_tape_csv,
        chunksize=chunksize,
        dtype=dtypes,
        usecols=usecols,
        keep_default_na=False,
        na_values=[""],
    )
    for chunk in reader:
        # Reduce this chunk to per-packet aggregates.
        chunk["is_add"] = ((chunk["typ"] == "M") & (chunk["action"] == "0")).astype("int32")
        chunk["is_mod"] = ((chunk["typ"] == "M") & (chunk["action"] == "1")).astype("int32")
        chunk["is_del"] = ((chunk["typ"] == "M") & (chunk["action"] == "2")).astype("int32")
        chunk["is_trd"] = (chunk["typ"] == "T").astype("int32")

        g = chunk.groupby("packet_seq", sort=False)
        part = g.agg(
            transactTime_first=("transactTime", "min"),
            transactTime_last=("transactTime", "max"),
            sendingTime_first=("sendingTime", "min"),
            sendingTime_last=("sendingTime", "max"),
            handlerendtim=("handlerendtim", "first"),
            recv_time=("recv_time", "max"),
            max_idx_in_packet=("idx_in_packet", "max"),
            n_msgs=("typ", "size"),
            n_add=("is_add", "sum"),
            n_modify=("is_mod", "sum"),
            n_delete=("is_del", "sum"),
            n_trade=("is_trd", "sum"),
        ).reset_index()

        if agg is None:
            agg = part
        else:
            # Merge: packet_seq is monotone across chunks, but a single packet
            # can span a chunk boundary. Sum the counts and pick min/max on
            # timestamps to be safe.
            joined = pd.concat([agg, part], ignore_index=True)
            joined = joined.groupby("packet_seq", sort=False).agg(
                transactTime_first=("transactTime_first", "min"),
                transactTime_last=("transactTime_last", "max"),
                sendingTime_first=("sendingTime_first", "min"),
                sendingTime_last=("sendingTime_last", "max"),
                handlerendtim=("handlerendtim", "first"),
                recv_time=("recv_time", "max"),
                max_idx_in_packet=("max_idx_in_packet", "max"),
                n_msgs=("n_msgs", "sum"),
                n_add=("n_add", "sum"),
                n_modify=("n_modify", "sum"),
                n_delete=("n_delete", "sum"),
                n_trade=("n_trade", "sum"),
            ).reset_index()
            agg = joined

    if agg is None or len(agg) == 0:
        # Zero-row day (holiday / half-session / no matching secID). Emit an
        # empty parquet with the right schema so downstream code that opens
        # every session's parquet doesn't need to special-case missing files.
        print(f"[packet_tape] WARNING: {msg_tape_csv} has zero rows; "
              f"writing empty parquet", file=sys.stderr)
        agg = pd.DataFrame({
            "packet_seq": pd.Series(dtype="int64"),
            "transactTime_first": pd.Series(dtype="int64"),
            "transactTime_last": pd.Series(dtype="int64"),
            "sendingTime_first": pd.Series(dtype="int64"),
            "sendingTime_last": pd.Series(dtype="int64"),
            "handlerendtim": pd.Series(dtype="int64"),
            "recv_time": pd.Series(dtype="int64"),
            "max_idx_in_packet": pd.Series(dtype="int32"),
            "n_msgs": pd.Series(dtype="int64"),
            "n_add": pd.Series(dtype="int32"),
            "n_modify": pd.Series(dtype="int32"),
            "n_delete": pd.Series(dtype="int32"),
            "n_trade": pd.Series(dtype="int32"),
        })
        Path(out_parquet).parent.mkdir(parents=True, exist_ok=True)
        agg.to_parquet(out_parquet, compression="snappy", index=False)
        return agg

    agg = agg.sort_values("packet_seq").reset_index(drop=True)
    # recv_time == 0 means "no trade in this packet carried it"; store as
    # int64 with 0 sentinel, not NaN, so the tape is a pure integer table.
    agg["recv_time"] = agg["recv_time"].fillna(0).astype("int64")

    Path(out_parquet).parent.mkdir(parents=True, exist_ok=True)
    agg.to_parquet(out_parquet, compression="snappy", index=False)
    print(f"[packet_tape] wrote {len(agg):,} packet rows to {out_parquet}",
          file=sys.stderr)
    return agg


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--msg-tape", required=True, help="msgtape CSV (per securityID)")
    ap.add_argument("--out", required=True, help="output parquet path")
    ap.add_argument("--chunksize", type=int, default=2_000_000,
                    help="rows per chunk for streaming (default 2M)")
    args = ap.parse_args()
    build_packet_tape(args.msg_tape, args.out, chunksize=args.chunksize)
    return 0


if __name__ == "__main__":
    sys.exit(main())
