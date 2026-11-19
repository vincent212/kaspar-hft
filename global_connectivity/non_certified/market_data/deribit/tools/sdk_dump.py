#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# sdk_dump.py: decode a Starbase market-data capture with Deribit's own Python
# SDK (starbase-deribit-md-sdk 2.0) and print every packet and message in the
# format `starbase_pcap --dump` prints, so the generated C++ decoder can be
# compared with an independent one (`make sdk-check`). Python 3.13+; the SDK is
# put on PYTHONPATH by the Makefile, not installed.

import dataclasses
import struct
import sys

from starbase_deribit_md_sdk.gen.md_messages import MESSAGE_REGISTRY
from starbase_deribit_md_sdk.packet import decode_md_message_header, decode_packet_header


def udp_payloads(path):
    d = open(path, "rb").read()
    off = 24
    while off + 16 <= len(d):
        incl = struct.unpack_from("<I", d, off + 8)[0]
        off += 16
        f = d[off:off + incl]
        off += incl
        o = 14
        et = struct.unpack_from(">H", f, 12)[0]
        if et == 0x8100:
            et = struct.unpack_from(">H", f, 16)[0]
            o = 18
        if et != 0x0800 or f[o + 9] != 17:
            continue
        ihl = (f[o] & 15) * 4
        ulen = struct.unpack_from(">H", f, o + ihl + 4)[0]
        yield f[o + ihl + 8:o + ihl + ulen]


def val(v):
    if isinstance(v, bool):
        return str(int(v))
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return str(struct.unpack("<q", struct.pack("<d", v))[0])
    if isinstance(v, str):
        return '"' + v + '"'
    if isinstance(v, list):
        return "[" + ";".join(",".join(val(getattr(e, f.name)) for f in dataclasses.fields(e)) for e in v) + "]"
    for attr in ("mantissa", "value"):   # Price9, sets
        if hasattr(v, attr):
            return str(int(getattr(v, attr)))
    return str(int(v))   # enums


def main(path):
    out = sys.stdout
    for pay in udp_payloads(path):
        ph = decode_packet_header(pay, 0)
        out.write(f"P {ph.sending_time} {ph.seq_num} {ph.channel_id} {val(ph.pkt_type)} {ph.message_count}\n")
        off = 24
        for i in range(ph.message_count):
            h = decode_md_message_header(pay, off)
            line = f"M {ph.seq_num + i} {h.message_length} {h.template_id} {h.version} {val(h.flags)} {h.transact_time}"
            cls = MESSAGE_REGISTRY.get(h.template_id)
            if cls is not None and h.template_id not in (200, 202):
                msg, _ = cls.decode(pay, off + 16)
                line += "".join(" " + val(getattr(msg, f.name)) for f in dataclasses.fields(msg))
            out.write(line + "\n")
            off += h.message_length


if __name__ == "__main__":
    main(sys.argv[1])
