#!/usr/bin/env python3

# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Generate HLS field decoders for CME MDP3 messages from CME's SBE template
# (templates_FixBinary.xml), the way genschema/ generates the C++ codecs:
# a new template version is regenerated, not rewritten.
#
#   python3 gen_decoder.py <templates_FixBinary.xml> <out.hpp> [template ids...]
#
# For each selected message it emits a struct with the template id, block
# length, a Root struct and decoder for the root block, and an entry struct,
# decoder and group-header reader for each repeating group, in wire order.
# Every field decodes to a plain integer (little-endian bytes, as on the
# wire); a composite with one non-constant member (PRICE9's mantissa) becomes
# that member, constants (no bytes on the wire) are left out, and a field newer
# than the message's version reads as its null value. Kept to C++14 for Vitis
# HLS: no pointers kept, fixed-size reads, everything inlined.

import sys
import xml.etree.ElementTree as ET

PRIM = {  # primitive -> (bytes, C type)
    "char": (1, "uint8_t"), "int8": (1, "int8_t"), "uint8": (1, "uint8_t"),
    "int16": (2, "int16_t"), "uint16": (2, "uint16_t"),
    "int32": (4, "int32_t"), "uint32": (4, "uint32_t"),
    "int64": (8, "int64_t"), "uint64": (8, "uint64_t"),
}
DEFAULT_TEMPLATES = [46, 48, 30, 4]


def local(tag):
    return tag.split("}")[-1].split(":")[-1]


class Schema:
    def __init__(self, path):
        root = ET.parse(path).getroot()
        self.id = int(root.get("id"))
        self.version = int(root.get("version"))
        self.types = {}
        for types in root.iter():
            if local(types.tag) != "types":
                continue
            for t in types:
                self.types[t.get("name")] = t
        self.messages = {int(m.get("id")): m for m in root.iter() if local(m.tag) == "message"}

    def resolve(self, name):
        """A type name -> (kind, element)."""
        t = self.types.get(name)
        if t is None:
            if name in PRIM:
                return "prim", name
            raise KeyError(name)
        return local(t.tag), t

    def scalar(self, name):
        """A scalar type -> (bytes, C type, null value or None, constant?)."""
        kind, t = self.resolve(name)
        if kind == "prim":
            n, c = PRIM[t]
            return n, c, None, False
        if kind == "type":
            if t.get("presence") == "constant":
                return 0, None, None, True
            n, c = PRIM[t.get("primitiveType")]
            length = int(t.get("length", "1"))
            if length != 1:
                return n * length, ("array", c, length), None, False
            return n, c, t.get("nullValue"), False
        if kind in ("enum", "set"):
            n, c, null, _ = self.scalar(t.get("encodingType"))
            return n, c, null, False
        raise ValueError("not a scalar: %s" % name)

    def composite_members(self, t):
        """(name, offset, bytes, C type, null) for the non-constant members."""
        out, off = [], 0
        for m in t:
            if local(m.tag) not in ("type", "ref"):
                continue
            if m.get("offset") is not None:
                off = int(m.get("offset"))
            if local(m.tag) == "ref":
                n, c, null, const = self.scalar(m.get("type"))
            elif m.get("presence") == "constant":
                n, c, null, const = 0, None, None, True
            else:
                n, c = PRIM[m.get("primitiveType")]
                null, const = m.get("nullValue"), False
            if not const:
                out.append((m.get("name"), off, n, c, null))
            off += n
        return out, off

    def field(self, f):
        """A message field -> list of (C name, offset, bytes, C type, null)."""
        name, ftype = f.get("name"), f.get("type")
        if f.get("presence") == "constant":
            return []
        kind, t = self.resolve(ftype)
        if kind == "composite":
            members, _ = self.composite_members(t)
            if len(members) == 1:
                _, off, n, c, null = members[0]
                return [(name, off, n, c, null)]
            return [("%s_%s" % (name, m), off, n, c, null) for m, off, n, c, null in members]
        n, c, null, const = self.scalar(ftype)
        if const:
            return []
        return [(name, 0, n, c, null)]


def cnull(c, null):
    if null is None:
        return "0"
    if c == "int64_t":
        return "INT64_C(%s)" % null
    if c == "uint64_t":
        return "UINT64_C(%s)" % null
    return "static_cast<%s>(%s)" % (c, null)


def emit_fields(s, fields, indent):
    """Struct members and the body of its decoder."""
    members, body = [], []
    off = 0
    for f in fields:
        if local(f.tag) != "field":
            continue
        if f.get("offset") is not None:
            off = int(f.get("offset"))
        since = int(f.get("sinceVersion", "0"))
        parts = s.field(f)
        size = 0
        for cname, moff, n, c, null in parts:
            size = max(size, moff + n)
            if isinstance(c, tuple):
                _, ec, length = c
                members.append("%s%s %s[%d];" % (indent, ec, cname, length))
                body.append("%sfor (int i = 0; i < %d; ++i)\n%s{\n#pragma HLS UNROLL\n%s  r.%s[i] = b[at + %d + i];\n%s}"
                            % (indent, length, indent, indent, cname, off + moff, indent))
                continue
            members.append("%s%s %s;" % (indent, c, cname))
            read = "le<%s, %d>(b, at + %d)" % (c, n, off + moff)
            if since:
                read = "version >= %d ? %s : %s" % (since, read, cnull(c, null))
            body.append("%sr.%s = %s;" % (indent, cname, read))
        off += size
    return members, body


def group_header(s, dim):
    _, t = s.resolve(dim)
    members, size = s.composite_members(t)
    pos = {m: (o, n, c) for m, o, n, c, _ in members}
    bl, ng = pos["blockLength"], pos["numInGroup"]
    return size, bl, ng


def generate(path, out, ids):
    s = Schema(path)
    w = []
    w.append("#pragma once\n")
    w.append("// Generated by actors/fpga/mdp3/gen_decoder.py from %s" % path.split("/")[-1])
    w.append("// (schema %d, version %d). Do not edit: regenerate." % (s.id, s.version))
    w.append("//")
    w.append("// HLS field decoders for CME MDP3 messages. `b` is the packet's bytes, `at`")
    w.append("// the offset of a root block or group entry; every read is little-endian,")
    w.append("// fixed size, and inlined.\n")
    w.append("#include <cstdint>\n")
    w.append("namespace mdp3gen {\n")
    w.append("constexpr uint16_t kSchemaId = %d;" % s.id)
    w.append("constexpr uint16_t kSchemaVersion = %d;\n" % s.version)
    w.append("// N bytes at b[at], little-endian, as a T.")
    w.append("template <class T, int N>")
    w.append("inline T le(const uint8_t *b, int at)")
    w.append("{")
    w.append("#pragma HLS INLINE")
    w.append("  uint64_t v = 0;")
    w.append("  for (int i = 0; i < N; ++i)")
    w.append("  {")
    w.append("#pragma HLS UNROLL")
    w.append("    v |= static_cast<uint64_t>(b[at + i]) << (8 * i);")
    w.append("  }")
    w.append("  return static_cast<T>(v);")
    w.append("}\n")
    w.append("// A repeating group's header: entry size and count, and the header's own size.")
    w.append("struct GroupHeader")
    w.append("{")
    w.append("  uint16_t block_length;")
    w.append("  uint16_t count;")
    w.append("  uint8_t size;")
    w.append("};\n")

    for tid in ids:
        m = s.messages[tid]
        name = m.get("name")
        w.append("// %s: template %d" % (name, tid))
        w.append("struct %s" % name)
        w.append("{")
        w.append("  static constexpr uint16_t kTemplateId = %d;" % tid)
        w.append("  static constexpr uint16_t kBlockLength = %s;\n" % m.get("blockLength"))
        members, body = emit_fields(s, list(m), "    ")
        w.append("  struct Root")
        w.append("  {")
        w.extend(members)
        w.append("  };\n")
        w.append("  static Root root(const uint8_t *b, int at, uint16_t version)")
        w.append("  {")
        w.append("#pragma HLS INLINE")
        w.append("    (void)version;")
        w.append("    Root r;")
        w.extend(body)
        w.append("    return r;")
        w.append("  }")
        groups = [g for g in m if local(g.tag) == "group"]
        for gi, g in enumerate(groups):
            gname = g.get("name")
            hsize, (bo, bn, bc), (no, nn, nc) = group_header(s, g.get("dimensionType"))
            members, body = emit_fields(s, list(g), "    ")
            w.append("")
            w.append("  // group %d of %d, header %s (%d bytes)" % (gi + 1, len(groups), g.get("dimensionType"), hsize))
            w.append("  static GroupHeader %s_header(const uint8_t *b, int at)" % gname)
            w.append("  {")
            w.append("#pragma HLS INLINE")
            w.append("    GroupHeader h;")
            w.append("    h.block_length = le<uint16_t, %d>(b, at + %d);" % (bn, bo))
            w.append("    h.count = le<uint16_t, %d>(b, at + %d);" % (nn, no))
            w.append("    h.size = %d;" % hsize)
            w.append("    return h;")
            w.append("  }\n")
            w.append("  struct %s_Entry" % gname)
            w.append("  {")
            w.extend(members)
            w.append("  };\n")
            w.append("  static %s_Entry %s_entry(const uint8_t *b, int at, uint16_t version)" % (gname, gname))
            w.append("  {")
            w.append("#pragma HLS INLINE")
            w.append("    (void)version;")
            w.append("    %s_Entry r;" % gname)
            w.extend(body)
            w.append("    return r;")
            w.append("  }")
        w.append("};\n")

    w.append("} // namespace mdp3gen")
    with open(out, "w") as f:
        f.write("\n".join(w) + "\n")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit("usage: gen_decoder.py <templates_FixBinary.xml> <out.hpp> [template ids...]")
    ids = [int(x) for x in sys.argv[3:]] or DEFAULT_TEMPLATES
    generate(sys.argv[1], sys.argv[2], ids)
