# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Tests for the packet_tape builder."""

from __future__ import annotations

import pandas as pd
import pytest

from arrival_paper.packet_tape import build_packet_tape


class TestPacketTapeSingleMsg:
    def test_one_msg_per_packet(self, tmp_path, msg_writer, write_msg):
        # 3 packets, each with a single MBO Add
        msg = [
            msg_writer(ts=1_000, sending=1_000, handler=1_000, packet_seq=0,
                       idx_in_packet=0, typ="M", action="0", side="0",
                       pxd=1000.0, sz=1, oid=1),
            msg_writer(ts=2_000, sending=2_000, handler=2_000, packet_seq=1,
                       idx_in_packet=0, typ="M", action="0", side="0",
                       pxd=1000.0, sz=1, oid=2),
            msg_writer(ts=3_000, sending=3_000, handler=3_000, packet_seq=2,
                       idx_in_packet=0, typ="M", action="0", side="0",
                       pxd=1000.0, sz=1, oid=3),
        ]
        p = tmp_path / "msg.csv"
        write_msg(p, msg)
        out = tmp_path / "packet.parquet"
        df = build_packet_tape(str(p), str(out))
        assert len(df) == 3
        assert list(df["n_msgs"]) == [1, 1, 1]
        assert list(df["n_add"]) == [1, 1, 1]
        assert list(df["packet_seq"]) == [0, 1, 2]


class TestPacketTapeMultiMsg:
    def test_multi_msg_packets_aggregate(self, tmp_path, msg_writer, write_msg):
        # Packet 0: 3 msgs (2 add, 1 trade)
        # Packet 1: 1 msg (delete)
        # Packet 2: 4 msgs (1 add, 2 modify, 1 trade)
        msg = [
            msg_writer(ts=100, sending=100, handler=100, packet_seq=0,
                       idx_in_packet=0, typ="M", action="0", side="0",
                       pxd=1000.0, sz=1, oid=1),
            msg_writer(ts=101, sending=101, handler=100, packet_seq=0,
                       idx_in_packet=1, typ="M", action="0", side="1",
                       pxd=1100.0, sz=1, oid=2),
            msg_writer(ts=102, sending=102, handler=100, packet_seq=0,
                       idx_in_packet=2, typ="T", sz=1, oid=1),

            msg_writer(ts=200, sending=200, handler=200, packet_seq=1,
                       idx_in_packet=0, typ="M", action="2", side="0",
                       pxd=1000.0, sz=0, oid=1),

            msg_writer(ts=300, sending=300, handler=300, packet_seq=2,
                       idx_in_packet=0, typ="M", action="0", side="0",
                       pxd=1050.0, sz=2, oid=3),
            msg_writer(ts=301, sending=301, handler=300, packet_seq=2,
                       idx_in_packet=1, typ="M", action="1", side="0",
                       pxd=1050.0, sz=3, oid=3),
            msg_writer(ts=302, sending=302, handler=300, packet_seq=2,
                       idx_in_packet=2, typ="M", action="1", side="0",
                       pxd=1051.0, sz=3, oid=3),
            msg_writer(ts=303, sending=303, handler=300, packet_seq=2,
                       idx_in_packet=3, typ="T", sz=1, oid=3),
        ]
        p = tmp_path / "msg.csv"
        write_msg(p, msg)
        out = tmp_path / "packet.parquet"
        df = build_packet_tape(str(p), str(out))

        assert len(df) == 3
        p0 = df.iloc[0]
        assert p0["packet_seq"] == 0 and p0["n_msgs"] == 3
        assert p0["n_add"] == 2 and p0["n_trade"] == 1
        assert p0["transactTime_first"] == 100 and p0["transactTime_last"] == 102
        assert p0["max_idx_in_packet"] == 2

        p1 = df.iloc[1]
        assert p1["packet_seq"] == 1 and p1["n_msgs"] == 1
        assert p1["n_delete"] == 1

        p2 = df.iloc[2]
        assert p2["packet_seq"] == 2 and p2["n_msgs"] == 4
        assert p2["n_add"] == 1 and p2["n_modify"] == 2 and p2["n_trade"] == 1
        assert p2["max_idx_in_packet"] == 3


class TestPacketTapeChunkBoundary:
    """Force chunk boundaries mid-packet and confirm counts still aggregate."""

    def test_split_packet_across_chunks(self, tmp_path, msg_writer, write_msg):
        # One packet of 10 messages
        rows = []
        for i in range(10):
            rows.append(msg_writer(
                ts=1000 + i, sending=1000 + i, handler=500,
                packet_seq=42, idx_in_packet=i,
                typ="M", action="0", side="0",
                pxd=1000.0, sz=1, oid=100 + i,
            ))
        p = tmp_path / "msg.csv"
        write_msg(p, rows)
        out = tmp_path / "packet.parquet"
        # chunksize=3 forces the 10-msg packet across 4 chunks.
        df = build_packet_tape(str(p), str(out), chunksize=3)
        assert len(df) == 1
        row = df.iloc[0]
        assert row["packet_seq"] == 42
        assert row["n_msgs"] == 10
        assert row["n_add"] == 10
        assert row["max_idx_in_packet"] == 9
        assert row["transactTime_first"] == 1000
        assert row["transactTime_last"] == 1009


class TestPacketTapeMissingColumn:
    def test_missing_packet_seq_raises(self, tmp_path):
        # Write a msgtape WITHOUT packet_seq — the old schema.
        p = tmp_path / "msg.csv"
        with open(p, "w") as f:
            f.write("transactTime,sendingTime,handlerendtim,recv_time,typ,action,side,pxd,sz,orderID\n")
            f.write("1,1,1,,M,0,0,1000.0,1,1\n")
        out = tmp_path / "packet.parquet"
        with pytest.raises(RuntimeError, match="packet_seq"):
            build_packet_tape(str(p), str(out))


class TestPacketTapeRecvTime:
    def test_recv_time_stored_as_int_zero_on_mbo_only_packet(
        self, tmp_path, msg_writer, write_msg
    ):
        msg = [
            msg_writer(ts=1, sending=1, handler=1, packet_seq=0,
                       idx_in_packet=0, typ="M", action="0", side="0",
                       pxd=1000.0, sz=1, oid=1),
        ]
        p = tmp_path / "msg.csv"
        write_msg(p, msg)
        out = tmp_path / "packet.parquet"
        df = build_packet_tape(str(p), str(out))
        # No recv_time on MBO → stored as 0
        assert df["recv_time"].iloc[0] == 0
        assert df["recv_time"].dtype.name == "int64"

    def test_recv_time_picked_up_from_trade_row(
        self, tmp_path, msg_writer, write_msg
    ):
        msg = [
            msg_writer(ts=1, sending=1, handler=1, packet_seq=0,
                       idx_in_packet=0, typ="M", action="0", side="0",
                       pxd=1000.0, sz=1, oid=1),
            msg_writer(ts=2, sending=2, handler=1, packet_seq=0,
                       idx_in_packet=1, typ="T", sz=1, oid=1,
                       recv_time=98765),
        ]
        p = tmp_path / "msg.csv"
        write_msg(p, msg)
        out = tmp_path / "packet.parquet"
        df = build_packet_tape(str(p), str(out))
        assert df["recv_time"].iloc[0] == 98765
