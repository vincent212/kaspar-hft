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
LOWER_IDENT = re.compile(r"^[a-z_][a-z0-9_]*$")
CPP_RESERVED = set("""
alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t
char16_t char32_t class compl concept const consteval constexpr constinit const_cast
continue co_await co_return co_yield decltype default delete do double dynamic_cast
else enum explicit export extern false float for friend goto if inline int long
mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected
public register reinterpret_cast requires return short signed sizeof static
static_assert static_cast struct switch template this thread_local throw true try
typedef typeid typename union unsigned using virtual void volatile wchar_t while xor
xor_eq
""".split())
# Names the generator defines in a struct, and in a venue or section namespace.
GENERATED_MEMBERS = {"type"}
GENERATED_NAMES = {"be", "uint_n", "set_str", "get_str", "status", "decoded", "decode",
                   "encode", "name", "message_t"}
PREFIX_MAX = {1: 0xFF, 2: 0xFFFF, 4: 0xFFFFFFFF}


class TableError(Exception):
    pass


class Field:
    def __init__(self, name, type_name, ftype, offset, length, desc, line):
        self.name = name
        self.type_name = type_name
        self.kind = ftype["kind"]
        self.align = ftype["align"]   # text: "left" (padded on the right) or "right"
        self.pad = ftype["pad"]       # text: "space" or "nul"
        self.offset = offset
        self.length = length          # None for a trailing variable-length field
        self.desc = desc
        self.line = line


class Message:
    def __init__(self, code, name, desc, line, sec=None):
        self.sec = sec                # the section that defines it
        self.code = code              # one character, or None for a message without one
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
        self.code_bytes = 0           # 0: one-character codes; 1, 2, 4: unsigned integer codes
        self.offset_base = 0          # offsets in the table start at this number
        self.framed = []              # extra decoders: (name, (bytes, inclusive|exclusive), line)
        self.includes = []            # (section name, line) whose messages this one also decodes
        self.included = []            # resolved: messages of included sections
        self.messages = []


class Table:
    def __init__(self):
        self.venue = None
        self.endian = None
        self.pad = "space"
        self.types = {}               # type name -> {"kind", "align", "pad"}
        self.sections = []


def err(line, msg):
    raise TableError("line %d: %s" % (line, msg))


def num(s, line, what):
    try:
        return int(s)
    except ValueError:
        err(line, "%s %r is not a number" % (what, s))


def parse_prefix(args, line):
    if len(args) != 2 or args[1] not in ("inclusive", "exclusive"):
        err(line, "@length_prefix takes <bytes> inclusive|exclusive")
    n = num(args[0], line, "prefix length")
    if n not in (1, 2, 4):
        err(line, "length prefix must be 1, 2 or 4 bytes")
    return (n, args[1])


def parse_type(args, line, default_pad):
    usage = "@type <Name> uint|int|bytes, or @type <Name> char [left|right] [space|nul]"
    if len(args) < 2 or args[1] not in KINDS:
        err(line, usage)
    opts = args[2:]
    if opts and args[1] != "char":
        err(line, usage)
    ftype = {"kind": args[1], "align": "left", "pad": None}
    for o in opts:
        if o in ("left", "right"):
            ftype["align"] = o
        elif o in ("space", "nul"):
            ftype["pad"] = o
        else:
            err(line, usage)
    ftype["explicit_pad"] = ftype["pad"] is not None
    return ftype


def parse(path):
    t = Table()
    sec = None
    msg = None
    seen_sections = {}
    for ln, raw in enumerate(open(path, encoding="utf-8"), 1):
        if raw.lstrip().startswith("#") and not raw.startswith("###"):
            continue                   # a comment line; does not end a message
        body, _, desc = raw.partition("#") if not raw.startswith("###") else (raw, "", "")
        body = body.strip()
        desc = desc.strip()
        if raw.startswith("###"):
            name = raw[3:].strip().split()[0] if raw[3:].strip() else ""
            msg = None
            if name == "end":
                break
            if not re.match(r"^[a-z_][a-z0-9_]*::(from|to)_[a-z0-9_]+$", name):
                err(ln, "section must be named <ns>::from_<venue> or <ns>::to_<venue> in lower "
                    "case, got %r" % name)
            if name in seen_sections:
                err(ln, "section %s already declared at line %d: continue it there"
                    % (name, seen_sections[name]))
            seen_sections[name] = ln
            sec = Section(name, ln)
            t.sections.append(sec)
            continue
        if not body:
            msg = None                 # a blank line ends a message
            continue
        if body.startswith("@"):
            toks = body.split()
            d, args = toks[0], toks[1:]
            if d in ("@venue", "@size", "@endian", "@pad") and len(args) != 1:
                err(ln, "%s takes one argument" % d)
            if d == "@venue":
                if not LOWER_IDENT.match(args[0]) or args[0] in CPP_RESERVED:
                    err(ln, "@venue %r must be a lower-case identifier (section names use it), "
                        "not a C++ keyword" % args[0])
                t.venue = args[0]
            elif d == "@endian":
                if args[0] not in ("little", "big"):
                    err(ln, "@endian little|big")
                t.endian = args[0]
            elif d == "@pad":
                if args[0] not in ("space", "nul"):
                    err(ln, "@pad space|nul")
                t.pad = args[0]
            elif d == "@type":
                t.types[args[0] if args else ""] = parse_type(args, ln, t.pad)
            elif d == "@length_prefix":
                p = parse_prefix(args, ln)
                if msg is not None:
                    msg.prefix = p
                elif sec is not None:
                    if sec.messages:
                        err(ln, "a section-wide @length_prefix must come before the section's first "
                            "message; for one message, put it right after its MessageType row")
                    sec.prefix = p
                else:
                    err(ln, "@length_prefix outside a section")
            elif d == "@type_code":
                if sec is None or sec.messages:
                    err(ln, "@type_code goes at the start of a section, before its first message")
                if args == ["char"]:
                    sec.code_bytes = 0
                elif len(args) == 2 and args[0] == "uint" and args[1] in ("1", "2", "4"):
                    sec.code_bytes = int(args[1])
                else:
                    err(ln, "@type_code char, or @type_code uint 1|2|4")
            elif d == "@offset_base":
                if sec is None or sec.messages:
                    err(ln, "@offset_base goes at the start of a section, before its first message")
                if len(args) != 1:
                    err(ln, "@offset_base takes one number")
                sec.offset_base = num(args[0], ln, "@offset_base")
            elif d == "@framed_decode":
                if sec is None or sec.direction != "from":
                    err(ln, "@framed_decode belongs in a from_ section")
                if len(args) != 3 or not IDENT.match(args[0]):
                    err(ln, "@framed_decode <name> <bytes> inclusive|exclusive")
                sec.framed.append((args[0], parse_prefix(args[1:], ln), ln))
            elif d == "@include":
                if sec is None or len(args) != 1:
                    err(ln, "@include <ns>::<section> inside a section")
                sec.includes.append((args[0], ln))
            elif d == "@size":
                if msg is None:
                    err(ln, "@size outside a message")
                msg.size = num(args[0], ln, "@size")
            else:
                err(ln, "unknown directive %s" % d)
            continue
        if sec is None:
            err(ln, "field outside a section")
        toks = body.split()
        if toks[0] == "MessageType":
            if len(toks) != 4:
                err(ln, "MessageType <code> <offset> <length> # <MessageName> ...")
            code, off, length = toks[1], num(toks[2], ln, "offset"), num(toks[3], ln, "length")
            if msg is not None:
                err(ln, "MessageType inside a message: separate messages with a blank line")
            words = desc.split()
            if words:
                words[0] = words[0].rstrip(":.,;")
            if not words:
                err(ln, "the description of MessageType must start with the message name")
            if code == "-" and length == 0:
                code = None            # a header or bare datagram: a struct, not in decode
            elif sec.code_bytes:
                try:
                    code = int(code, 0)
                except ValueError:
                    err(ln, "type code %r is not a number (the section has @type_code uint)" % code)
                if length != sec.code_bytes:
                    err(ln, "type code length %d, @type_code says %d" % (length, sec.code_bytes))
                if not 0 <= code < 1 << (8 * length):
                    err(ln, "type code %d does not fit in %d bytes" % (code, length))
            elif len(code) != 1 or length != 1:
                err(ln, "a type code is one character of length 1, '- 0 0' for none, or a "
                    "number in a section with @type_code uint (code %r, length %d)" % (code, length))
            if off != sec.offset_base:
                err(ln, "MessageType must be at offset %d (the section's first offset; offsets "
                    "exclude any length prefix unless @offset_base says otherwise)" % sec.offset_base)
            off -= sec.offset_base
            msg = Message(code, words[0], desc, ln, sec)
            sec.messages.append(msg)
            if code is not None and sec.code_bytes:
                msg.fields.append(Field("MessageType", "code", {"kind": "uint", "align": "left",
                                                                "pad": "space"},
                                        0, length, desc, ln))
            elif code is not None:
                msg.fields.append(Field("MessageType", "char",
                                        {"kind": "char", "align": "left", "pad": "space"},
                                        0, 1, desc, ln))
            continue
        if msg is None:
            err(ln, "field before MessageType")
        if len(toks) == 3 and toks[1] in t.types and t.types[toks[1]]["kind"] == "bytes":
            name, tname, off, length = toks[0], toks[1], num(toks[2], ln, "offset"), None
        elif len(toks) == 4:
            name, tname, off, length = (toks[0], toks[1], num(toks[2], ln, "offset"),
                                        num(toks[3], ln, "length"))
        else:
            err(ln, "expected <Name> <Type> <Offset> <Length> # <description>")
        if tname not in t.types:
            err(ln, "type %r not declared with @type" % tname)
        if off < sec.offset_base:
            err(ln, "offset %d is below the section's @offset_base %d" % (off, sec.offset_base))
        off -= sec.offset_base
        ftype = dict(t.types[tname])
        if not ftype["explicit_pad"]:
            ftype["pad"] = t.pad
        msg.fields.append(Field(name, tname, ftype, off, length, desc, ln))
    if t.venue is None or t.endian is None:
        raise TableError("the table must declare @venue and @endian")
    return t


def typed_messages(sec):
    """Messages decoded or encoded in sec: those it includes, then its own."""
    return sec.included + [m for m in sec.messages if m.code is not None]


def resolve_includes(t):
    by_ns = {}
    for sec in t.sections:
        for name, ln in sec.includes:
            src = by_ns.get(name)
            if src is None:
                err(ln, "@include %s: no earlier section of that name" % name)
            if src.direction != sec.direction:
                err(ln, "@include %s: it is a %s_ section, this is a %s_ section"
                    % (name, src.direction, sec.direction))
            if src.code_bytes != sec.code_bytes:
                err(ln, "@include %s: its type codes differ in form from this section's" % name)
            sec.included += [m for m in src.messages if m.code is not None]
        by_ns[sec.ns] = sec


def check(t):
    resolve_includes(t)
    for sec in t.sections:
        if not re.match(r"^[a-z_0-9]+::(from|to)_%s(_[a-z0-9_]+)?$" % re.escape(t.venue), sec.ns):
            err(sec.line, "section %s is not <ns>::from_%s[_<layer>] or <ns>::to_%s[_<layer>]"
                % (sec.ns, t.venue, t.venue))
        if sec.direction == "from" and len({prefix_of(m.sec, m) for m in typed_messages(sec)}) > 1:
            err(sec.line, "messages in %s differ in their length prefix, so a message cannot be "
                "identified before it is decoded: put each framing in its own section" % sec.ns)
        if sec.framed and any(prefix_of(m.sec, m) for m in typed_messages(sec)):
            err(sec.framed[0][2], "@framed_decode adds a length prefix to a section that has one")
        codes = {}
        names = set()
        for m in sec.included:
            if m.code in codes:
                err(sec.line, "%s includes type code %r twice" % (sec.ns, m.code))
            codes[m.code] = m.line
            names.add(m.name)
        for m in sec.messages:
            if not IDENT.match(m.name) or m.name in CPP_RESERVED or m.name in GENERATED_NAMES:
                err(m.line, "message name %r is a C++ keyword or a name the generator uses "
                    "(%s)" % (m.name, ", ".join(sorted(GENERATED_NAMES))))
            if m.code is not None and m.code in codes:
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
                if not IDENT.match(f.name) or f.name in CPP_RESERVED or f.name in GENERATED_MEMBERS:
                    err(f.line, "field name %r is not a usable C++ identifier" % f.name)
                if f.name == "MessageLength" and prefix_of(sec, m):
                    err(f.line, "MessageLength is the name of the generated length prefix: this "
                        "message has both; drop the row or the @length_prefix")
                if f.name == "MessageType" and f.line != m.line:
                    err(f.line, "MessageType is reserved for the type-code row")
                if f.name in seen or "set_" + f.name in seen or "get_" + f.name in seen:
                    err(f.line, "field %s repeated, or clashes with a generated accessor" % f.name)
                seen.add(f.name)
                if f.kind == "char" and f.length and f.length > 1:
                    seen.update(("set_" + f.name, "get_" + f.name))
                if f.offset != pos:
                    err(f.line, "%s at offset %d, expected %d: a field is missing, or a length is wrong"
                        % (f.name, f.offset + sec.offset_base, pos + sec.offset_base))
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


def cchar(c):
    """A C++ character literal for c."""
    if c == "'":
        return "'\\''"
    if c == "\\":
        return "'\\\\'"
    if " " <= c <= "~":
        return "'%s'" % c
    return "'\\x%02x'" % ord(c)


def ctype(t, f):
    if f.kind == "char":
        return "char"
    if f.kind == "bytes":
        return "uint8_t"
    if f.length not in INT_LENGTHS:
        return "uint_n<%d, %s>" % (f.length, "true" if t.endian == "big" else "false")
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
    # A // comment ends at the line; a trailing backslash would continue it.
    return " ".join(s.split()).rstrip("\\ ")


COMMON = """// Shared by every header generated for this venue; defined once.
#ifndef FIELDGEN_%(guard)s_COMMON
#define FIELDGEN_%(guard)s_COMMON

// A big-endian integer as it appears on the wire. Reads and writes convert.
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

// An unsigned integer of N bytes as it appears on the wire, most significant
// byte first if Big.
template <size_t N, bool Big>
struct [[gnu::packed]] uint_n
{
  uint8_t b[N];
  operator uint64_t() const
  {
    uint64_t v = 0;
    for (size_t i = 0; i < N; ++i)
      v |= uint64_t(b[Big ? N - 1 - i : i]) << (8 * i);
    return v;
  }
  uint_n &operator=(uint64_t v)
  {
    for (size_t i = 0; i < N; ++i)
      b[Big ? N - 1 - i : i] = uint8_t(v >> (8 * i));
    return *this;
  }
};

// Copy s into a fixed-width text field: left-aligned (padded on the right) or
// right-aligned (padded on the left), with the given pad character. Text
// longer than the field is cut.
template <size_t N>
inline void set_str(char (&dst)[N], std::string_view s, char pad = ' ', bool right = false)
{
  const size_t n = s.size() < N ? s.size() : N;
  if (right)
  {
    std::memset(dst, pad, N - n);
    std::memcpy(dst + N - n, s.data(), n);
  }
  else
  {
    std::memcpy(dst, s.data(), n);
    std::memset(dst + n, pad, N - n);
  }
}

// A fixed-width text field without the spaces and NULs that pad it, at
// either end.
template <size_t N>
inline std::string_view get_str(const char (&src)[N])
{
  size_t b = 0, e = N;
  while (e > 0 && (src[e - 1] == ' ' || src[e - 1] == '\\0'))
    --e;
  while (b < e && (src[b] == ' ' || src[b] == '\\0'))
    ++b;
  return std::string_view(src + b, e - b);
}

// The result of decode(). ok: a message was decoded. incomplete: the buffer
// ends before the message does; read more and decode again from the same
// place. unknown: a type code the table does not list; size is the bytes to
// skip when the framing gives it, otherwise 0 and the stream cannot be
// followed. malformed: the length prefix is shorter than the message's fixed
// part, or than the type code; size as for unknown.
enum class status
{
  ok,
  incomplete,
  unknown,
  malformed,
};

#endif // FIELDGEN_%(guard)s_COMMON

"""


def emit(t, out):
    w = out.write
    w("// Generated by fieldgen.py from a field table. Do not edit; edit the table.\n")
    w("#pragma once\n\n")
    for h in ("cstddef", "cstdint", "cstring", "ostream", "string", "string_view", "type_traits",
              "variant"):
        w("#include <%s>\n" % h)
    w("\nnamespace %s\n{\n\n" % t.venue)
    w(COMMON % {"guard": t.venue.upper()})
    for sec in t.sections:
        ns = sec.ns.split("::")
        w("namespace %s::%s\n{\n\n" % (ns[0], ns[1]))
        for m in sec.included:
            sns = m.sec.ns.split("::")
            w("using %s = ::%s::%s::%s::%s; // from %s\n" % (m.name, t.venue, sns[0], sns[1], m.name,
                                                         m.sec.ns))
        if sec.included:
            w("\n")
        for m in sec.messages:
            emit_message(t, sec, m, w)
        names = [m.name for m in typed_messages(sec)]
        if names:
            w("using message_t = std::variant<%s>;\n\n" % ", ".join(names))
            if sec.direction == "from":
                emit_decode(t, sec, w)
            w("inline const char *name(const message_t &m)\n{\n")
            w("  return std::visit([](const auto &x) -> const char * {\n")
            w("    using T = std::decay_t<decltype(x)>;\n")
            for i, n in enumerate(names):
                w("    %sif constexpr (std::is_same_v<T, %s>) return \"%s\";\n"
                  % ("" if i == 0 else "else ", n, n))
            w("    else return \"?\";\n  }, m);\n}\n\n")
        w("} // namespace %s::%s\n\n" % (ns[0], ns[1]))
    w("} // namespace %s\n" % t.venue)


def emit_message(t, sec, m, w):
    p = prefix_of(sec, m)
    w("// %s\n" % comment(m.desc))
    w("struct [[gnu::packed]] %s\n{\n" % m.name)
    if m.code is not None and sec.code_bytes:
        w("  static constexpr uint%d_t type = %d;\n" % (8 * sec.code_bytes, m.code))
    elif m.code is not None:
        w("  static constexpr char type = %s;\n" % cchar(m.code))
    if p:
        w("  %s MessageLength; // %s length prefix, not in the venue's table\n"
          % (length_type(t, p[0]), p[1]))
    for f in m.fields:
        if f.length is None:
            w("  // %s: variable length, follows this struct. %s\n" % (f.name, comment(f.desc)))
            continue
        w("  %s // %s\n" % (member(t, f), comment(f.desc)))
    for f in m.fields:
        if f.kind == "char" and f.length and f.length > 1:
            pad = "' '" if f.pad == "space" else "'\\0'"
            w("  void set_%s(std::string_view s) { set_str(%s, s, %s, %s); }\n"
              % (f.name, f.name, pad, "true" if f.align == "right" else "false"))
            w("  std::string_view get_%s() const { return get_str(%s); }\n" % (f.name, f.name))
    w("};\n")
    base = p[0] if p else 0
    w("static_assert(sizeof(%s) == %d);\n" % (m.name, base + size_of(m)))
    for f in m.fields:
        if f.length is not None:
            w("static_assert(offsetof(%s, %s) == %d);\n" % (m.name, f.name, base + f.offset))
    w("\n")
    w("inline std::ostream &operator<<(std::ostream &o, const %s &m)\n{\n" % m.name)
    w('  (void)m;\n  o << "%s";\n' % m.name)
    for f in m.fields:
        if f.length is None or f.name == "MessageType":
            continue
        if f.kind == "char" and f.length > 1:
            w('  o << " %s=" << get_str(m.%s);\n' % (f.name, f.name))
        elif f.kind == "bytes":
            w('  o << " %s=<%d bytes>";\n' % (f.name, f.length))
        elif f.kind == "char":
            w('  o << " %s=" << m.%s;\n' % (f.name, f.name))
        elif f.length == 1:
            w('  o << " %s=" << +m.%s;\n' % (f.name, f.name))
        else:
            cast = "uint64_t" if f.length not in INT_LENGTHS else ctype(t, f).replace("be<", "").rstrip(">")
            w('  o << " %s=" << %s(m.%s);\n' % (f.name, cast, f.name))
    w("  return o;\n}\n\n")
    if sec.direction == "to":
        emit_encode(t, m, p, w)


def emit_encode(t, m, p, w):
    tail = m.variable_tail
    w("// Set the type and length of m and copy it to buf (cap bytes)%s. Returns the\n"
      % (", followed by %s" % tail.name if tail else ""))
    w("// bytes written, or 0 if they do not fit in cap%s.\n"
      % (" or the length prefix" if p else ""))
    w("inline size_t encode(char *buf, size_t cap, %s m%s)\n{\n"
      % (m.name, ", std::string_view %s" % tail.name if tail else ""))
    w("  const size_t total = sizeof(m)%s;\n" % (" + %s.size()" % tail.name if tail else ""))
    w("  if (total > cap)\n    return 0;\n")
    if m.code is not None:
        w("  m.MessageType = %s::type;\n" % m.name)
    if p:
        w("  const size_t length = total%s;\n" % ("" if p[1] == "inclusive" else " - %d" % p[0]))
        w("  if (length > %d)\n    return 0;\n" % PREFIX_MAX[p[0]])
        w("  m.MessageLength = uint%d_t(length);\n" % (8 * p[0]))
    w("  std::memcpy(buf, &m, sizeof(m));\n")
    if tail:
        w("  std::memcpy(buf + sizeof(m), %s.data(), %s.size());\n" % (tail.name, tail.name))
    w("  return total;\n}\n\n")


def emit_decode(t, sec, w):
    typed = typed_messages(sec)
    p = prefix_of(typed[0].sec, typed[0])
    off = p[0] if p else 0
    w("""// One decoded message. When st is ok, msg holds it, size is the bytes it
// occupies including any length prefix (the next message starts at
// buf + size), and tail and tail_len are its variable-length field, if it has
// one. Otherwise see status.
struct decoded
{
  status st;
  message_t msg;
  size_t size = 0;
  const char *tail = nullptr;
  size_t tail_len = 0;
  explicit operator bool() const { return st == status::ok; }
};

// Decode the message at buf (at its length prefix, if it has one); len is the
// bytes available. A length prefix longer than the fixed part of a
// fixed-size message is accepted and the extra bytes skipped (later protocol
// versions append fields).
inline decoded decode(const char *buf, size_t len)
{
""")
    cb = sec.code_bytes or 1
    w("  if (len < %d)\n    return {status::incomplete, {}};\n" % (off + cb))
    skip = "0"
    if p:
        w("  %s prefix;\n  std::memcpy(&prefix, buf, sizeof(prefix));\n" % length_type(t, p[0]))
        w("  const size_t total = size_t(uint%d_t(prefix))%s;\n"
          % (8 * p[0], "" if p[1] == "inclusive" else " + %d" % p[0]))
        w("  if (total < %d)\n    return {status::malformed, {}, total};\n" % (off + cb))
        w("  if (total > len)\n    return {status::incomplete, {}};\n")
        skip = "total"
    if sec.code_bytes:
        w("  %s code;\n  std::memcpy(&code, buf + %d, sizeof(code));\n"
          % (length_type(t, sec.code_bytes), off))
        w("  switch (uint%d_t(code))\n  {\n" % (8 * sec.code_bytes))
    else:
        w("  switch (buf[%d])\n  {\n" % off)
    for m in typed:
        tail = m.variable_tail is not None
        w("  case %s::type:\n  {\n" % m.name)
        if p:
            w("    if (total < sizeof(%s))\n      return {status::malformed, {}, total};\n" % m.name)
            size = "total"
        else:
            w("    if (len < sizeof(%s))\n      return {status::incomplete, {}};\n" % m.name)
            size = "len" if tail else "sizeof(%s)" % m.name
        w("    %s m;\n    std::memcpy(&m, buf, sizeof(m));\n" % m.name)
        if tail:
            w("    return {status::ok, m, %s, buf + sizeof(m), %s - sizeof(m)};\n" % (size, size))
        else:
            w("    return {status::ok, m, %s};\n" % size)
        w("  }\n")
    w("  }\n  return {status::unknown, {}, %s};\n}\n\n" % skip)
    for name, (n, kind), _ in sec.framed:
        w("// Decode a message carried with a %d-byte %s length prefix in front (as when\n"
          % (n, kind))
        w("// this layer is framed by %s). size includes the prefix.\n" % name)
        w("inline decoded decode_%s(const char *buf, size_t len)\n{\n" % name)
        w("  if (len < %d)\n    return {status::incomplete, {}};\n" % n)
        w("  %s prefix;\n  std::memcpy(&prefix, buf, sizeof(prefix));\n" % length_type(t, n))
        w("  const size_t total = size_t(uint%d_t(prefix))%s;\n"
          % (8 * n, "" if kind == "inclusive" else " + %d" % n))
        w("  if (total < %d)\n    return {status::malformed, {}, total};\n" % (n + cb))
        w("  if (total > len)\n    return {status::incomplete, {}};\n")
        w("  decoded d = decode(buf + %d, total - %d);\n" % (n, n))
        w("  if (d.st == status::incomplete)\n    return {status::malformed, {}, total};"
          " // shorter than its message\n")
        w("  d.size = total;\n  return d;\n}\n\n")


def markdown(t, out):
    w = out.write
    for sec in t.sections:
        w("### %s\n\n" % sec.ns)
        if sec.includes:
            w("Also decodes the messages of %s.\n\n" % ", ".join("`%s`" % n for n, _ in sec.includes))
        if sec.framed:
            w("Also decoded with a length prefix in front: %s.\n\n" % ", ".join(
                "`decode_%s` (%d-byte, %s)" % (n, p[0], p[1]) for n, p, _ in sec.framed))
        for m in sec.messages:
            p = prefix_of(sec, m)
            w("#### %s (type %s, %d bytes%s)\n\n" % (
                m.name, ("`%d` (0x%X)" % (m.code, m.code) if isinstance(m.code, int) else
                         "`%s`" % m.code if m.code is not None else "none"), size_of(m),
                ", %d-byte %s length prefix" % p if p else ""))
            w("| Field | Offset | Length | Type | Meaning |\n|---|---|---|---|---|\n")
            for f in m.fields:
                tname = f.type_name
                if f.kind == "char" and f.length and f.length > 1 and f.align == "right":
                    tname += " (right-aligned)"
                w("| %s | %d | %s | %s | %s |\n" % (
                    f.name, f.offset + sec.offset_base, "var" if f.length is None else f.length, tname,
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
