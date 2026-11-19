# Fixed-offset binary protocols: field table and generator

For ITCH-, OUCH-, Glimpse- and SoupBinTCP-style protocols, whose specifications
give each message as a table of fields at fixed offsets, the decoders are not
written by hand. The agent writes a **field table** from the specification and
runs `tools/fieldgen.py`, which checks the table and generates the C++ header.
The field table is the reviewable artefact: it is what the user checks against
the PDF in step 4, and the code follows from it mechanically.

This replaces the per-venue `msg_gen.py` scripts in the m2 repository (Fenics UST,
Dealerweb). From the m2 tables converted to this format, the struct sizes and field
offsets of the generated Fenics (338 checks) and Dealerweb (312 checks) headers match
the m2 headers, except the size of the one message whose variable-length payload m2
holds as a pointer member (below). The conversion needed the session-layer and
application-layer messages of two Dealerweb sections (`om::from_dealerweb`,
`glimpse::from_dealerweb`) split into separate sections, which the m2 tables mix
(below). `fieldgen.py` also differs from the m2 scripts in what it rejects and in the
code it emits.

## Workflow

1. Extract every message of the specification into a field table
   (`<venue>.tbl`). One subagent per group of messages for a long specification;
   each cross-checks its tables against `pdftotext -layout` output.
2. `fieldgen.py <venue>.tbl --check` until it passes. Each error names a line.
   Fix the table from the PDF; never adjust a number to make the check pass.
3. `fieldgen.py <venue>.tbl --markdown` produces the body of section 4 (message
   catalogue) of the feed description, with `###` per section and `####` per
   message. The user reviews that, not the table.
4. After approval: `fieldgen.py <venue>.tbl -o <venue>/include/<venue>/msg_def.hpp`.
   Commit the table and the generator invocation (a Makefile rule), so that the
   header is regenerated when the table changes. Never edit the header.
5. Write the dispatcher and the session actors on top of the generated types.

## Table format

```
@venue nasdaq                 # C++ namespace, lower case; section names use it
@endian big                   # byte order of every integer in the table
@pad space                    # default padding of text fields: space | nul
@type Integer uint            # map each type name used in the spec to a kind:
@type Price uint              #   uint, int, char (text), bytes (opaque)
@type Alpha char              # text, left-aligned (padded on the right)
@type Numeric char right      # text, right-aligned (padded on the left);
                              #   a pad (space | nul) may follow

### itch::from_nasdaq         # <namespace>::from_<venue>[_<layer>]  (received, decoded)
@framed_decode mold 2 exclusive  # also decode_mold(): each message after a 2-byte
                              #   length (MoldUDP64) that is not in the spec's
                              #   tables; exclusive = the length does not count itself
MessageType A 0 1 # AddOrder: description; the first word names the message
@size 36                      # the total the spec states; checked
StockLocate Integer 1 2 # ...
...                           # a blank line ends the message
### itch::to_nasdaq           # sent: encoders are generated
### end
```

- Columns: `Name Type Offset Length # description`. Offsets are 0-based and
  relative to the start of the message as the specification tables it; a length
  prefix declared with `@length_prefix` is not counted in them.
- `###`, a space and a section name (indented or not) starts a section. Any other
  line starting with `#` (a `#####` banner, a `####` heading) is a comment and may
  appear anywhere, including inside a message.
- The first row of each message is `MessageType <code> 0 1`, where the code is any
  one character. A structure without a type code (a packet header such as
  MoldUDP64's, or a bare request datagram) is written `MessageType - 0 0`: it gets a
  struct (and an `encode` in a `to_` section) but is not part of `decode`, since
  nothing identifies it; the packet reader reads it at the position the framing
  gives.
- Feeds that identify messages by a number rather than a character (NYSE Pillar
  XDP, Cboe PITCH, Deutsche Börse T7 EOBI, HKEX OMD) declare it at the start of the
  section with `@type_code uint 1|2|4`; the codes are then written as numbers,
  decimal or `0x` hexadecimal (`MessageType 100 0 2`). The integer has the table's
  byte order. Where the message size precedes the type and counts itself, as in
  these feeds, it is an `@length_prefix ... inclusive`.
- Where the specification counts a length field in its offsets (SoupBinTCP's own
  tables, IEX's SNAP), copy its offsets as they are and declare
  `@offset_base <first offset>` at the start of the section, rather than shifting
  every offset by hand.
- One message layer often arrives under two framings: ITCH inside MoldUDP64 blocks
  (each message after a 2-byte length) and inside SoupBinTCP packets (no length).
  Write the layer once, without a length prefix, and add
  `@framed_decode <name> <bytes> inclusive|exclusive`; the generator then emits
  `decode_<name>()` for the framed form besides `decode()`.
- A layer that carries another layer's messages plus its own (GLIMPSE: the ITCH
  messages and End of Snapshot) declares `@include <ns>::<section>` for the earlier
  section and adds only its own messages; the included structs are aliased, not
  copied, and its `decode` covers both. Includes are transitive: a section that
  includes one which includes a third decodes the third's messages too.
- Sections of another table file are used with `@import <file> [<ns>::<section>
  ...]` before the first section; the path is relative to the importing table.
  Without section names every section of the file is imported. Imported sections
  keep their own venue, byte order and types, are generated into the importing
  table's header, and can be `@include`d (Cboe Europe reusing the US unit header,
  GRP and Spin sections; GLIMPSE including ITCH from `itch.tbl`). An imported
  layer is written without the framing of any one transport, as `@include`
  requires: ITCH written with a section-wide `@length_prefix` cannot be included
  by GLIMPSE, whose messages have none; write it unframed with
  `@framed_decode mold 2 exclusive`. The Makefile rule for the importing table
  lists the imported tables among its prerequisites.
- Where every message starts with the same fields and the type code is one of
  them (Aquis: type, then length, then sequence number; Currenex OUCH: sequence
  number, timestamp, then type), declare them once at the start of the section
  with `@header <Name>`, one row per field up to the next blank line, the type
  code's row written `MessageType code <offset> <length>`. Each message then starts
  with `MessageType <code> <offset> <length>` at the header's offset of the code,
  and its own fields continue from the end of the header; offsets are the
  specification's, counted from the first header byte. Where a header field gives
  the message length, `@message_length <field> whole|body` in the header block
  says so (`whole`: the whole message, header included; `body`: the bytes after
  the header), and `decode` takes the message size from it: unknown types are
  skipped by it and longer messages accepted. A header is not combined with a
  `@length_prefix`.
- One type code with two layouts:
  - told apart by the message length (NYSE Pillar's Refresh Header, 16 bytes in a
    symbol's first refresh packet and 8 in the later ones): write both messages
    with the same code and `@by_length` in the second; `decode` returns the largest
    layout that fits the length. The section needs the length from its framing
    (`@length_prefix`, or `@message_length`); the layouts differ in size and have
    no variable-length field.
  - told apart by context (Cboe's Trading Status, one layout on the equities
    feeds and another on the options feeds): one section, or one table, per
    context; the handler calls the `decode` of the feed it reads.
- A message may have the name of one of its fields (a field `Symbol` in a message
  `Symbol`). The structs have no constructor, so C++ accepts a data member named
  like its class; `tools/test_fieldgen.sh` compiles one.
- Alignment and padding of text belong to its type. Where the specification pads
  some fields of one type differently (SoupBinTCP's session id is left padded,
  its username right padded), declare a second type name for them.
- A section-wide `@length_prefix` must come before the section's first message. To
  frame one message only, put `@length_prefix` directly after its `MessageType` row.
  In a `from_` section every message must have the same framing, because the type
  code can only be found once the framing is known; messages framed differently
  belong to different layers and go in different sections.
- A trailing variable-length field is written without a length
  (`Payload Binary 3`); it must be the last field and its type must be `bytes`.
- Unsigned integers may be 1 to 8 bytes (ITCH timestamps are 6). Signed integers
  must be 1, 2, 4 or 8 bytes. Wider numeric fields are `bytes`.
- Copy field names from the specification, made into identifiers; keep reserved
  and filler fields with their offsets. C++ keywords are refused as message and
  field names, and so are the names the generator uses: `type`, `MessageType`
  (other than the type-code row), `MessageLength` in a message with a length
  prefix, `set_<field>` and `get_<field>` for a text field, `be`, `uint_n`,
  `set_str`, `get_str` and the `<cstdint>` type names (as a field or as an accessor
  a text field generates), and, for messages, `be`, `uint_n`, `set_str`, `get_str`,
  `status`, `decoded`, `decode`, `encode`, `name` and `message_t`. A message needs
  at least one fixed-length field.
- A section appears once in a table; one that continues after a page break is
  continued in place.
- Put the specification's section number in each message's description.
- Separate protocol layers into separate sections: the session layer (login,
  heartbeat, sequenced-data envelope) and the application messages it carries
  (`om::from_<venue>` and `om::from_<venue>_app`). The session layer's decode
  returns the envelope's payload; the application layer's decode is called on it.
  The m2 Dealerweb table mixes the two in its order-entry and Glimpse sections, and
  the m2 Fenics table in its order-entry section, where a session message and an
  order message share type `O`.
- The rule applies where the protocol has two layers. Where session and order
  messages share one header, one sequence and one type-code space (Currenex OUCH:
  Logon, Heartbeat and the order messages are all one layer behind the same
  header), they are one section per direction; the feed description says which
  messages are session messages.

Worked examples from public Nasdaq specifications:

| Table | Specification | Shows |
|---|---|---|
| `examples/itch50.tbl` | TotalView-ITCH 5.0 (revision of April 28, 2023) | market data, 6-byte integers, one layer decoded bare (SoupBinTCP) and with MoldUDP64's 2-byte length (`@framed_decode`) |
| `examples/soupbintcp.tbl` | SoupBinTCP 3.0 | a session layer in both directions, variable-length payloads, right-aligned text |

`tools/test_fieldgen.sh` generates both headers, runs `examples/*_test.cpp` against
them (the SoupBinTCP test includes both headers, as a handler for ITCH over
SoupBinTCP would), checks that malformed tables are rejected with the line named,
and compiles and runs tables for each directive: escaped and numeric type codes,
`@include` and `@framed_decode`, `@import` (within a venue and across venues,
three headers in one translation unit, two versions of one section refused),
`@header` with and without `@message_length` (the Aquis and Currenex shapes),
`@by_length` (NYSE's two Refresh Headers), and a message named like its field. Run
it with each compiler the handlers are built with (`CXX=g++-15
tools/test_fieldgen.sh`).

## What the check rejects

| Check | Catches |
|---|---|
| each offset equals the previous offset plus length | a dropped row (a table split across pages), a wrong length, 1-based offsets |
| `@size` equals the sum of lengths | the same, against the total the spec states |
| integer lengths | a numeric field mistyped as an integer |
| repeated type code in a `from_` section (a warning in a `to_` section) | two layers mixed in one section |
| a numeric type code that is not a number, does not fit its width, or comes in a section without `@type_code uint` | a misread type table |
| messages framed differently in one `from_` section; a section-wide `@length_prefix` after the first message | two layers mixed in one section; a prefix meant for one message |
| a variable-length field that is not last | a misread table |
| non-numeric offsets, lengths and sizes; missing directive arguments | extraction debris |
| repeated message or field name; a field or accessor that clashes with another or hides a generated helper; a repeated `@framed_decode` name; a message with no fixed-length field | copy-paste rows; names the compiler would reject |
| a repeated section | a section continued after a page break |
| `@include` of an unknown, later or differently coded section, of a section with another header or byte order; a type code in both the including and the included section; offsets below `@offset_base`; `@framed_decode` on a section that already has a length prefix | a misdeclared layer |
| `@import` of a missing file or section, after the first section, in a cycle, or of a section the table also defines | a misdeclared import |
| a `@header` after the first message, without a `MessageType code` row, with a code length other than `@type_code`'s, or combined with a `@length_prefix`; a `@message_length` field that is not an unsigned header field; a message whose `MessageType` row is not at the header's code; a message field that repeats a header field | a misdeclared header |
| `@by_length` without an earlier message of that code, in a `from_` section whose framing gives no length, or for layouts of equal size or with a variable-length field | layouts that cannot be told apart |
| undeclared type names | a type the spec uses that was not mapped to a kind |
| section names; a venue that is not a lower-case identifier | a section that does not say its direction and venue |

## What is generated

One header, namespace `<venue>::<ns>::<from|to>_<venue>`. The helpers below that
every header for a venue shares (`be`, `uint_n`, `set_str`, `get_str`, `status`) are
defined once in namespace `<venue>`, behind an include guard, so several headers for
one venue (ITCH and SoupBinTCP, say) can be included together. Each section is
behind a guard made of its name and a hash of its generated code: a section that
appears in two headers (its own table's and an importing table's) is compiled once
when both are included, and two different versions of it in one translation unit
fail to compile.

- `[[gnu::packed]]` structs named after the message, with `static constexpr char type`,
  and `static_assert`s on `sizeof` and on `offsetof` of every field, so that a
  compiler or packing difference fails the build; in a section with a `@header`,
  every message struct begins with the header's fields, and the header is also a
  struct of its own (for reading the header of a message `decode` does not know);
- big-endian integers as `be<T>` and odd-width integers as `uint_n<N, Big>`, which
  convert on read and write, so handler code never calls a byte-swap;
- for each text field longer than one character, `set_<field>(std::string_view)`,
  which pads and aligns as the field's type says, and `get_<field>()`, which returns
  it without its padding; `set_str` and `get_str` do the same for any field, with the
  pad and alignment given;
- `operator<<` for every message (used by `--dump` monitors and test output);
- for `from_` sections: `using message_t = std::variant<...>` and
  `decoded decode(const char *buf, size_t len)`. `decoded` has a `status` and, when
  it is `ok`, the message, `size` (the bytes it occupies, prefix included, so the
  next message starts at `buf + size`) and `tail`/`tail_len` (the variable-length
  field, for messages that have one); it converts to `true` only when `ok`. The other
  statuses tell the caller what to do: `incomplete` (the buffer ends inside the
  message: read more, decode again from the same place), `unknown` (a type the table
  does not list) and `malformed` (a length prefix or `@message_length` shorter than
  the message's fixed part); for these two, `size` is the bytes to skip when the
  framing gives it, and 0 when it does not, when the length is shorter than the
  header, or when an inclusive prefix is smaller than its own width: size 0 means
  the stream cannot be followed. A length longer than the fixed part of a
  fixed-size message is accepted and the extra bytes skipped (newer protocol
  versions append fields).
  Messages are copied out with `memcpy`;
- for `to_` sections: `size_t encode(char *buf, size_t cap, Msg m)`, or
  `encode(char *buf, size_t cap, Msg m, std::string_view tail)` for a message with a
  variable-length field; it sets the type and the length prefix (counting the tail),
  writes the tail after the struct, and returns the bytes written, or 0 if they do
  not fit in `cap` or the length does not fit in the prefix; in a section with a
  `@header`, it sets the header's `@message_length` field instead of a prefix;
- `name(const message_t &)`.

Differences from the m2 generators, each a defect the m2 output has:

- m2 represents a variable-length trailing field as a `void *` member, which adds 8
  bytes to the struct, so its decode copies 8 bytes beyond the message. Here the
  field is not a member; the payload follows the struct in the buffer.
- m2's decode does not check the buffer length, does not read the length prefix,
  and cannot tell a partial message from an unknown one.
- m2 hardcodes the type-code position (`buf[2]` or `buf[0]`) and the table path in
  each venue's script, and its two scripts differ in column order and type names.

## The handler on top of the generated code

The generated header only parses bytes. The hand-written parts, from the nearest
existing handler (`mdp3/` for structure; the m2 Fenics handler for a fixed-offset
venue):

| Part | Responsibility |
|---|---|
| packet reader | multicast A/B sockets (reuse the existing arbitration), the packet header (session, sequence number, message count), splitting the packet into messages by their length prefixes |
| dispatcher | `decode`, then `std::visit` or `holds_alternative` per message; maps each venue message to `bfile::l3_t` records as section 7 of the feed description says; keeps the order-ID to instrument map and the instrument directory; sends `endOfEvent` and the end-of-packet record |
| sequencer | gap detection per sequence scope; on a gap, emits the gap record and starts recovery |
| recovery session | the TCP snapshot or retransmission session (`to_`/`from_` sections of its own); aligns the snapshot with the buffered stream; emits the channel-reset record and clears per-channel state |
| instrument loader | from the directory message or an external file; tick, price decimals, quantity multiplier per instrument |
| factories and config | `interface/<venue>/if/*.hpp`, an ini section per channel |

Unit tests decode bytes written out by hand from the specification's examples
(as `examples/itch50_test.cpp` does), and check every record the dispatcher emits
for one message of each type.
