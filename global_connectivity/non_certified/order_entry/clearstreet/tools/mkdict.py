#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
"""Clear Street FIX 4.2 dictionary for QuickFIX, from QuickFIX's FIX42.xml.

Clear Street publishes no dictionary file, only the FIX API guide
(docs.clearstreet.com/guides/fix-api, feed.md section 1). The guide uses
FIX 4.2 plus fields FIX 4.2 does not have in these messages. This script adds
exactly those, each with the guide section that requires it:

  Logon (A)          Username (553), Password (554)       "Logon (35=A)"
                     Text (58), on Clear Street's Logon   "Logon (35=A)": accounts list
  NewOrderSingle (D) OpenClose (77)                       "Submitting an order"
  and replace (G)    TradingSessionID (336), top level    "Submitting an order", "Extended hours"
                     TargetStrategy (847)                 "Execution strategies"
                     SecurityType (167) already in FIX 4.2
  TimeInForce (59)   value 7, At the Close                "Time in force" (a FIX 4.3 value)
  OrderCancelRequest SecurityType already in FIX 4.2

Usage: mkdict.py FIX42.xml OUT.xml
"""
import sys
import xml.etree.ElementTree as ET

NEW_FIELDS = [
    ("553", "Username", "STRING"),
    ("554", "Password", "STRING"),
    ("847", "TargetStrategy", "INT"),
]
MSG_ADD = {
    "A": [("Username", "N"), ("Password", "N"), ("Text", "N")],
    "D": [("OpenClose", "N"), ("TradingSessionID", "N"), ("TargetStrategy", "N")],
    "G": [("OpenClose", "N"), ("TradingSessionID", "N"), ("TargetStrategy", "N")],
}


def main(src, out):
    tree = ET.parse(src)
    root = tree.getroot()
    fields = root.find("fields")
    by_name = {f.get("name"): f for f in fields}
    by_num = {f.get("number"): f for f in fields}
    for num, name, typ in NEW_FIELDS:
        if num in by_num:
            if by_num[num].get("name") != name:
                sys.exit(f"tag {num} already defined as {by_num[num].get('name')}")
            continue
        f = ET.SubElement(fields, "field", number=num, name=name, type=typ)
        by_name[name] = f
        by_num[num] = f
    tif = by_name["TimeInForce"]
    if not any(v.get("enum") == "7" for v in tif):
        ET.SubElement(tif, "value", enum="7", description="AT_THE_CLOSE")
    msgs = {m.get("msgtype"): m for m in root.find("messages")}
    for mt, add in MSG_ADD.items():
        m = msgs[mt]
        # direct children only: FIX 4.2 has TradingSessionID inside the
        # NoTradingSessions group, Clear Street sends it at the top level
        have = {f.get("name") for f in m if f.tag == "field"}
        for name, req in add:
            if name not in by_name:
                sys.exit(f"field {name} missing from {src}")
            if name not in have:
                ET.SubElement(m, "field", name=name, required=req)
    tree.write(out, encoding="utf-8", xml_declaration=True)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
