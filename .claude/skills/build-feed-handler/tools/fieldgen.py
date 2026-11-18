#!/usr/bin/env python3
"""Generate C++ message structs, decoders and encoders from a field table.

A field table describes a fixed-offset binary protocol (ITCH-style market data,
OUCH-style order entry, snapshot/recovery sessions). The format is defined in
../binary_codegen.md. The output is one self-contained C++20 header.

    fieldgen.py TABLE                 write the header to stdout
    fieldgen.py TABLE -o msg_def.hpp  write the header to a file
    fieldgen.py TABLE --check         validate the table only
    fieldgen.py TABLE --markdown      render the message catalogue for review
"""

import argparse
import re
import sys

INT_LENGTHS = (1, 2, 4, 8)
KINDS = ("uint", "int", "char", "bytes")
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
CPP_RESERVED = {
    "class", "delete", "default", "new", "operator", "private", "public", "register",
    "struct", "switch", "template", "this", "union", "volatile", "namespace", "int",
    "char", "long", "short", "signed", "unsigned", "float", "double", "auto", "case",
    "const", "do", "else", "enum", "for", "goto", "if", "return", "static", "while",
}


class TableError(Exception):
    pass


class Field:
    def __init__(self, name, type_name, kind, offset, length, desc, line):
        self.name = name
        self.type_name = type_name
        self.kind = kind
        self.offset = offset
        self.length = length          # None for a trailing variable-length field
        self.desc = desc
        self.line = line


class Message:
    def __init__(self, code, name, desc, line):
        self.code = code
        self.name = name
        self.desc = desc
        self.line = line
        self.fields = []
        self.prefix = None            # (length, "inclusive" | "exclusive") or None
        self.size = None              # declared size, checked if given
        self.variable_tail = None     # trailing variable-length field, if any


class Section:
    def __init__(self, ns, line):
        self.ns = ns
        self.line = line
        self.direction = "from" if "::from_" in ns else "to"
        self.prefix = None
        self.messages = []


class Table:
    def __init__(self):
        self.venue = None
        self.endian = None
        self.pad = "space"
        self.types = {}               # type name -> kind
        self.sections = []


def err(line, msg):
    raise TableError("line %d: %s" % (line, msg))


def parse_prefix(args, line):
    if len(args) != 2 or args[1] not in ("inclusive", "exclusive"):
        err(line, "@length_prefix takes <bytes> inclusive|exclusive")
    n = int(args[0])
    if n not in (1, 2, 4):
        err(line, "length prefix must be 1, 2 or 4 bytes")
    return (n, args[1])


def parse(path):
    t = Table()
    sec = None
    msg = None
    for ln, raw in enumerate(open(path, encoding="utf-8"), 1):
        body, _, desc = raw.partition("#") if not raw.startswith("###") else (raw, "", "")
        body = body.strip()
        desc = desc.strip()
        if raw.startswith("###"):
            name = raw[3:].strip().split()[0] if raw[3:].strip() else ""
            msg = None
            if name == "end":
                break
            if not re.match(r"^[a-z_][a-z0-9_]*::(from|to)_[a-z0-9_]+$", name):
                err(ln, "section must be named <ns>::from_<venue> or <ns>::to_<venue>, got %r" % name)
            sec = Section(name, ln)
            t.sections.append(sec)
            continue
        if not body:
            msg = None                 # a blank line ends a message
            continue
        if body.startswith("@"):
            toks = body.split()
            d, args = toks[0], toks[1:]
            if d == "@venue":
                t.venue = args[0]
            elif d == "@endian":
                if args != ["little"] and args != ["big"]:
                    err(ln, "@endian little|big")
                t.endian = args[0]
            elif d == "@pad":
                if args not in (["space"], ["nul"]):
                    err(ln, "@pad space|nul")
                t.pad = args[0]
            elif d == "@type":
                if len(args) != 2 or args[1] not in KINDS:
                    err(ln, "@type <Name> uint|int|char|bytes")
                t.types[args[0]] = args[1]
            elif d == "@length_prefix":
                p = parse_prefix(args, ln)
                if msg is not None:
                    msg.prefix = p
                elif sec is not None:
                    sec.prefix = p
                else:
                    err(ln, "@length_prefix outside a section")
            elif d == "@size":
                if msg is None:
                    err(ln, "@size outside a message")
                msg.size = int(args[0])
            else:
                err(ln, "unknown directive %s" % d)
            continue
        if sec is None:
            err(ln, "field outside a section")
        toks = body.split()
        if toks[0] == "MessageType":
            if len(toks) != 4:
                err(ln, "MessageType <code> <offset> <length> # <MessageName> ...")
            code, off, length = toks[1], int(toks[2]), int(toks[3])
            if msg is not None:
                err(ln, "MessageType inside a message: separate messages with a blank line")
            words = desc.split()
            if words:
                words[0] = words[0].rstrip(":.,;")
            if not words or not IDENT.match(words[0]):
                err(ln, "the description of MessageType must start with the message name")
            if code == "-" and length == 0:
                if sec.direction != "to":
                    err(ln, "a message without a type code can only be sent, not decoded")
            elif len(code) != length or length != 1:
                err(ln, "type codes must be one character (code %r, length %d)" % (code, length))
            if off != 0:
                err(ln, "MessageType must be at offset 0 (offsets exclude any length prefix)")
            msg = Message(code, words[0], desc, ln)
            sec.messages.append(msg)
            if length:
                msg.fields.append(Field("MessageType", "char", "char", 0, 1, desc, ln))
            continue
        if msg is None:
            err(ln, "field before MessageType")
        if len(toks) == 3 and toks[1] in t.types and t.types[toks[1]] == "bytes":
            name, tname, off, length = toks[0], toks[1], int(toks[2]), None
        elif len(toks) == 4:
            name, tname, off, length = toks[0], toks[1], int(toks[2]), int(toks[3])
        else:
            err(ln, "expected <Name> <Type> <Offset> <Length> # <description>")
        if tname not in t.types:
            err(ln, "type %r not declared with @type" % tname)
        msg.fields.append(Field(name, tname, t.types[tname], off, length, desc, ln))
    if t.venue is None or t.endian is None:
        raise TableError("the table must declare @venue and @endian")
    return t


def check(t):
    for sec in t.sections:
        if not re.match(r"^[a-z_0-9]+::(from|to)_%s(_[a-z0-9_]+)?$" % re.escape(t.venue), sec.ns):
            err(sec.line, "section %s is not <ns>::from_%s[_<layer>] or <ns>::to_%s[_<layer>]"
                % (sec.ns, t.venue, t.venue))
        codes = {}
        names = set()
        for m in sec.messages:
            if m.code != "-" and m.code in codes:
                msg = ("type code %r already used at line %d: are two protocol layers in one section?"
                       % (m.code, codes[m.code]))
                if sec.direction == "from":
                    err(m.line, msg)
                print("warning: line %d: %s" % (m.line, msg), file=sys.stderr)
            codes[m.code] = m.line
            if m.name in names:
                err(m.line, "message name %s repeated in %s" % (m.name, sec.ns))
            names.add(m.name)
            seen = set()
            pos = 0
            for i, f in enumerate(m.fields):
                if not IDENT.match(f.name) or f.name in CPP_RESERVED:
                    err(f.line, "field name %r is not a usable C++ identifier" % f.name)
                if f.name in seen:
                    err(f.line, "field %s repeated" % f.name)
                seen.add(f.name)
                if f.offset != pos:
                    err(f.line, "%s at offset %d, expected %d: a field is missing, or a length is wrong"
                        % (f.name, f.offset, pos))
                if f.length is None:
                    if i != len(m.fields) - 1:
                        err(f.line, "a variable-length field must be last")
                    m.variable_tail = f
                    continue
                if f.length <= 0:
                    err(f.line, "length must be positive")
                if f.kind == "int" and f.length not in INT_LENGTHS:
                    err(f.line, "signed integer of %d bytes is not supported" % f.length)
                if f.kind == "uint" and f.length > 8:
                    err(f.line, "integer of %d bytes: declare it as char or bytes" % f.length)
                pos += f.length
            if m.size is not None and m.size != pos:
                err(m.line, "%s: fields add up to %d bytes, @size says %d" % (m.name, pos, m.size))


def prefix_of(sec, m):
    return m.prefix if m.prefix is not None else sec.prefix


def size_of(m):
    return sum(f.length for f in m.fields if f.length is not None)


def ctype(t, f):
    if f.kind == "char":
        return "char"
    if f.kind == "bytes":
        return "uint8_t"
    if f.length not in INT_LENGTHS:
        return "uint_n<%d>" % f.length
    base = ("uint%d_t" if f.kind == "uint" else "int%d_t") % (8 * f.length)
    if t.endian == "big" and f.length > 1:
        return "be<%s>" % base
    return base


def member(t, f):
    c = ctype(t, f)
    if f.kind in ("char", "bytes") and f.length > 1:
        return "%s %s[%d];" % (c, f.name, f.length)
    return "%s %s;" % (c, f.name)


def length_type(t, n):
    base = "uint%d_t" % (8 * n)
    return "be<%s>" % base if t.endian == "big" and n > 1 else base


def comment(s):
    return s.replace("*/", "* /")


def emit(t, out):
    w = out.write
    w("// Generated by fieldgen.py from a field table. Do not edit; edit the table.\n")
    w("#pragma once\n\n")
    for h in ("cstddef", "cstdint", "cstring", "optional", "ostream", "string", "string_view", "variant"):
        w("#include <%s>\n" % h)
    w("\nnamespace %s\n{\n\n" % t.venue)
    if t.endian == "big":
        w("""// A big-endian integer as it appears on the wire. Reads and writes convert.
template <typename T>
struct [[gnu::packed]] be
{
  T raw;
  static constexpr T swap(T v)
  {
    if constexpr (sizeof(T) == 2) return T(__builtin_bswap16(uint16_t(v)));
    else if constexpr (sizeof(T) == 4) return T(__builtin_bswap32(uint32_t(v)));
    else if constexpr (sizeof(T) == 8) return T(__builtin_bswap64(uint64_t(v)));
    else return v;
  }
  operator T() const { return swap(raw); }
  be &operator=(T v) { raw = swap(v); return *this; }
};

""")
    if any(f.kind == "uint" and f.length not in INT_LENGTHS and f.length is not None
           for s_ in t.sections for m in s_.messages for f in m.fields):
        w("""// An unsigned integer of N bytes (N not 1, 2, 4 or 8) as it appears on the wire.
template <size_t N>
struct [[gnu::packed]] uint_n
{
  uint8_t b[N];
  operator uint64_t() const
  {
    uint64_t v = 0;
    for (size_t i = 0; i < N; ++i)
      v |= uint64_t(b[%s]) << (8 * i);
    return v;
  }
  uint_n &operator=(uint64_t v)
  {
    for (size_t i = 0; i < N; ++i)
      b[%s] = uint8_t(v >> (8 * i));
    return *this;
  }
};

""" % (("N - 1 - i", "N - 1 - i") if t.endian == "big" else ("i", "i")))
    pad = "' '" if t.pad == "space" else "'\\0'"
    w("""// Copy s into a fixed-width field, padded as the venue requires.
template <size_t N>
inline void set_str(char (&dst)[N], std::string_view s)
{
  const size_t n = s.size() < N ? s.size() : N;
  std::memcpy(dst, s.data(), n);
  std::memset(dst + n, %s, N - n);
}

// A fixed-width field without its padding.
template <size_t N>
inline std::string_view get_str(const char (&src)[N])
{
  size_t n = N;
  while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\\0'))
    --n;
  return std::string_view(src, n);
}

""" % pad)
    for sec in t.sections:
        ns = sec.ns.split("::")
        w("namespace %s::%s\n{\n\n" % (ns[0], ns[1]))
        for m in sec.messages:
            p = prefix_of(sec, m)
            w("// %s\n" % comment(m.desc))
            w("struct [[gnu::packed]] %s\n{\n" % m.name)
            if m.code != "-":
                w("  static constexpr char type = '%s';\n" % m.code)
            if p:
                w("  %s MessageLength; // %s length prefix, not in the venue's table\n"
                  % (length_type(t, p[0]), "inclusive" if p[1] == "inclusive" else "exclusive"))
            for f in m.fields:
                if f.length is None:
                    w("  // %s: variable length, follows this struct. %s\n" % (f.name, comment(f.desc)))
                    continue
                w("  %s // %s\n" % (member(t, f), comment(f.desc)))
            w("};\n")
            base = p[0] if p else 0
            w("static_assert(sizeof(%s) == %d);\n" % (m.name, base + size_of(m)))
            for f in m.fields:
                if f.length is not None:
                    w("static_assert(offsetof(%s, %s) == %d);\n" % (m.name, f.name, base + f.offset))
            w("\n")
            # printing
            w("inline std::ostream &operator<<(std::ostream &o, const %s &m)\n{\n" % m.name)
            w('  (void)m;\n  o << "%s";\n' % m.name)
            for f in m.fields:
                if f.length is None or f.name == "MessageType":
                    continue
                if f.kind == "char" and f.length > 1:
                    w('  o << " %s=" << get_str(m.%s);\n' % (f.name, f.name))
                elif f.kind == "bytes":
                    w('  o << " %s=<%d bytes>";\n' % (f.name, f.length))
                elif f.kind == "char" or f.length == 1:
                    w('  o << " %s=" << +m.%s;\n' % (f.name, f.name) if f.kind != "char"
                      else '  o << " %s=" << m.%s;\n' % (f.name, f.name))
                else:
                    cast = "uint64_t" if f.length not in INT_LENGTHS else ctype(t, f).replace("be<", "").rstrip(">")
                    w('  o << " %s=" << %s(m.%s);\n' % (f.name, cast, f.name))
            w("  return o;\n}\n\n")
            if sec.direction == "to":
                w("// Fill the type and length of m and copy it to buf. Returns the bytes written.\n")
                w("inline size_t encode(char *buf, %s m)\n{\n" % m.name)
                if m.code != "-":
                    w("  m.MessageType = %s::type;\n" % m.name)
                if p:
                    expr = "sizeof(m)" if p[1] == "inclusive" else "sizeof(m) - sizeof(m.MessageLength)"
                    w("  m.MessageLength = uint%d_t(%s);\n" % (8 * p[0], expr))
                w("  std::memcpy(buf, &m, sizeof(m));\n  return sizeof(m);\n}\n\n")
        names = [m.name for m in sec.messages]
        if names:
            w("using message_t = std::variant<%s>;\n\n" % ", ".join(names))
            prefixes = {prefix_of(sec, m) for m in sec.messages}
            if sec.direction == "from":
                w("// Decode one message. buf points at the message (at its length prefix, if it\n")
                w("// has one); len is the bytes available. Unknown types and short buffers\n")
                w("// return nullopt. Bytes beyond the struct (newer versions) are ignored.\n")
                w("inline std::optional<message_t> decode(const char *buf, size_t len)\n{\n")
                if len(prefixes) == 1:
                    p = prefixes.pop()
                    off = p[0] if p else 0
                    w("  if (len <= %d)\n    return std::nullopt;\n" % off)
                    w("  switch (buf[%d])\n  {\n" % off)
                    for m in sec.messages:
                        w("  case %s::type:\n" % m.name)
                        w("    if (len < sizeof(%s))\n      return std::nullopt;\n" % m.name)
                        w("    { %s m; std::memcpy(&m, buf, sizeof(m)); return m; }\n" % m.name)
                    w("  }\n  return std::nullopt;\n}\n\n")
                else:
                    w("  // Messages in this section differ in their length prefix: try each layout.\n")
                    for m in sec.messages:
                        p = prefix_of(sec, m)
                        off = p[0] if p else 0
                        w("  if (len > %d && buf[%d] == %s::type && len >= sizeof(%s))\n"
                          % (off, off, m.name, m.name))
                        w("  { %s m; std::memcpy(&m, buf, sizeof(m)); return m; }\n" % m.name)
                    w("  return std::nullopt;\n}\n\n")
            w("inline const char *name(const message_t &m)\n{\n")
            w("  return std::visit([](const auto &x) -> const char * {\n")
            w("    using T = std::decay_t<decltype(x)>;\n")
            for i, n in enumerate(names):
                w("    %sif constexpr (std::is_same_v<T, %s>) return \"%s\";\n" % ("" if i == 0 else "else ", n, n))
            w("    else return \"?\";\n  }, m);\n}\n\n")
        w("} // namespace %s::%s\n\n" % (ns[0], ns[1]))
    w("} // namespace %s\n" % t.venue)


def markdown(t, out):
    w = out.write
    for sec in t.sections:
        w("## %s\n\n" % sec.ns)
        for m in sec.messages:
            p = prefix_of(sec, m)
            w("### %s (type `%s`, %d bytes%s)\n\n" % (
                m.name, m.code, size_of(m),
                ", %d-byte %s length prefix" % p if p else ""))
            w("| Field | Offset | Length | Type | Meaning |\n|---|---|---|---|---|\n")
            for f in m.fields:
                w("| %s | %d | %s | %s | %s |\n" % (
                    f.name, f.offset, "var" if f.length is None else f.length, f.type_name,
                    f.desc.replace("|", "\\|")))
            w("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("table")
    ap.add_argument("-o", "--output")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--markdown", action="store_true")
    a = ap.parse_args()
    try:
        t = parse(a.table)
        check(t)
    except TableError as e:
        sys.exit("%s: %s" % (a.table, e))
    if a.check:
        n = sum(len(s.messages) for s in t.sections)
        print("%s: %d sections, %d messages, OK" % (a.table, len(t.sections), n))
        return
    out = open(a.output, "w") if a.output else sys.stdout
    (markdown if a.markdown else emit)(t, out)


if __name__ == "__main__":
    main()
