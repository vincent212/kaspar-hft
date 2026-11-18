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
@venue nasdaq                 # C++ namespace; section names must use it
@endian big                   # byte order of every integer in the table
@pad space                    # padding of fixed-width strings on encode: space | nul
@type Integer uint            # map each type name used in the spec to a kind:
@type Price uint              #   uint, int, char (text), bytes (opaque)
@type Alpha char

### itch::from_nasdaq         # <namespace>::from_<venue>[_<layer>]  (received, decoded)
@length_prefix 2 exclusive    # every message here is preceded by a 2-byte length
                              #   that is not in the spec's tables; exclusive =
                              #   the length does not count itself
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
- A line starting with `#` is a comment and may appear anywhere, including inside
  a message.
- The first row of each message is `MessageType <code> 0 1`. A message without a
  type code (a bare request datagram) is written `MessageType - 0 0` and can only
  appear in a `to_` section.
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
  and filler fields with their offsets. `type`, C++ keywords, `MessageType` (other
  than the type-code row) and, in a message with a length prefix, `MessageLength`
  are refused, because the generator uses them.
- Put the specification's section number in each message's description.
- Separate protocol layers into separate sections: the session layer (login,
  heartbeat, sequenced-data envelope) and the application messages it carries
  (`om::from_<venue>` and `om::from_<venue>_app`). The session layer's decode
  returns the envelope's payload; the application layer's decode is called on it.
  The m2 Dealerweb table mixes the two in its order-entry and Glimpse sections, and
  the m2 Fenics table in its order-entry section, where a session message and an
  order message share type `O`.

Worked examples from public Nasdaq specifications:

| Table | Specification | Shows |
|---|---|---|
| `examples/itch50.tbl` | TotalView-ITCH 5.0 (revision of April 28, 2023) | market data, 6-byte integers, a length prefix not in the spec's tables |
| `examples/soupbintcp.tbl` | SoupBinTCP 3.0 | a session layer in both directions, variable-length payloads |

`tools/test_fieldgen.sh` generates both headers, runs `examples/*_test.cpp` against
them, and checks that malformed tables are rejected with the line named.

## What the check rejects

| Check | Catches |
|---|---|
| each offset equals the previous offset plus length | a dropped row (a table split across pages), a wrong length, 1-based offsets |
| `@size` equals the sum of lengths | the same, against the total the spec states |
| integer lengths | a numeric field mistyped as an integer |
| repeated type code in a `from_` section (a warning in a `to_` section) | two layers mixed in one section |
| messages framed differently in one `from_` section; a section-wide `@length_prefix` after the first message | two layers mixed in one section; a prefix meant for one message |
| a variable-length field that is not last | a misread table |
| non-numeric offsets, lengths and sizes; missing directive arguments | extraction debris |
| repeated message or field name; keywords and the generator's own names | copy-paste rows; names the compiler would reject |
| undeclared type names | a type the spec uses that was not mapped to a kind |
| section names | a section that does not say its direction and venue |

## What is generated

One header, namespace `<venue>::<ns>::<from|to>_<venue>`:

- `[[gnu::packed]]` structs named after the message, with `static constexpr char type`,
  and `static_assert`s on `sizeof` and on `offsetof` of every field, so that a
  compiler or packing difference fails the build;
- big-endian integers as `be<T>` and odd-width integers as `uint_n<N>`, which convert
  on read and write, so handler code never calls a byte-swap;
- `set_str` and `get_str` for fixed-width text, padding and trimming as the table
  says;
- `operator<<` for every message (used by `--dump` monitors and test output);
- for `from_` sections: `using message_t = std::variant<...>` and
  `std::optional<decoded> decode(const char *buf, size_t len)`. `decoded` holds the
  message, `size` (the bytes it occupies, prefix included, so the next message
  starts at `buf + size`) and `tail`/`tail_len` (the variable-length field, for
  messages that have one). With a length prefix, decode reads it and returns
  `nullopt` if the prefix runs past `len` or is shorter than the message's fixed
  part; a prefix longer than the fixed part of a fixed-size message is accepted and
  the extra bytes skipped (newer protocol versions append fields). Unknown types
  return `nullopt`. Messages are copied out with `memcpy`;
- for `to_` sections: `size_t encode(char *buf, Msg m)`, or
  `encode(char *buf, Msg m, std::string_view tail)` for a message with a
  variable-length field; it sets the type and the length prefix (counting the tail),
  writes the tail after the struct, and returns the bytes written;
- `name(const message_t &)`.

Differences from the m2 generators, each a defect the m2 output has:

- m2 represents a variable-length trailing field as a `void *` member, which adds 8
  bytes to the struct, so its decode copies 8 bytes beyond the message. Here the
  field is not a member; the payload follows the struct in the buffer.
- m2's decode does not check the buffer length and does not read the length prefix.
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
