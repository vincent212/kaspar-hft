# Fixed-offset binary protocols: field table and generator

For ITCH-, OUCH-, Glimpse- and SoupBinTCP-style protocols, whose specifications
give each message as a table of fields at fixed offsets, the decoders are not
written by hand. The agent writes a **field table** from the specification and
runs `tools/fieldgen.py`, which checks the table and generates the C++ header.
The field table is the reviewable artefact: it is what the user checks against
the PDF in step 4, and the code follows from it mechanically.

This replaces the per-venue `msg_gen.py` scripts in the m2 repository (Fenics UST,
Dealerweb). `fieldgen.py` reproduces their struct layouts: from the m2 tables converted to this
format, the struct sizes and field offsets of the generated Fenics (338 checks) and
Dealerweb (312 checks) headers match the m2 headers, except the size of the one
message whose variable-length payload m2 holds as a pointer member (below). It differs from them
in what it rejects and in the code it emits (below).

## Workflow

1. Extract every message of the specification into a field table
   (`<venue>.tbl`). One subagent per group of messages for a long specification;
   each cross-checks its tables against `pdftotext -layout` output.
2. `fieldgen.py <venue>.tbl --check` until it passes. Each error names a line.
   Fix the table from the PDF; never adjust a number to make the check pass.
3. `fieldgen.py <venue>.tbl --markdown` produces section 4 (message catalogue)
   of the feed description. The user reviews that, not the table.
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
- The first row of each message is `MessageType <code> 0 1`. A message without a
  type code (a bare request datagram) is written `MessageType - 0 0` and can only
  appear in a `to_` section.
- `@length_prefix` may also follow a `MessageType` row, to apply to that message
  only (some sessions frame only some messages).
- A trailing variable-length field is written without a length
  (`Payload Binary 3`); it must be the last field and its type must be `bytes`.
- Unsigned integers may be 1 to 8 bytes (ITCH timestamps are 6). Signed integers
  must be 1, 2, 4 or 8 bytes. Wider numeric fields are `bytes`.
- Copy field names from the specification, made into identifiers; keep reserved
  and filler fields with their offsets.
- Put the specification's section number in each message's description.
- Separate protocol layers into separate sections: the session layer (login,
  heartbeat, sequenced-data envelope) and the application messages it carries
  (`om::to_<venue>` and `om::to_<venue>_app`). The check refuses a repeated type
  code in a `from_` section, since it makes decoding ambiguous, and warns about one
  in a `to_` section.

`examples/itch50.tbl` is a worked example from the public Nasdaq TotalView-ITCH 5.0
specification; `tools/test_fieldgen.sh` generates its header and runs
`examples/itch50_test.cpp` against it.

## What the check rejects

| Check | Catches |
|---|---|
| each offset equals the previous offset plus length | a dropped row (a table split across pages), a wrong length, 1-based offsets |
| `@size` equals the sum of lengths | the same, against the total the spec states |
| integer lengths | a numeric field mistyped as an integer |
| repeated type code in a `from_` section | two layers mixed in one section |
| repeated message or field name; invalid identifiers | copy-paste rows; names the compiler would reject |
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
  `std::optional<message_t> decode(const char *buf, size_t len)`, which returns
  `nullopt` for an unknown type or a buffer shorter than the struct, ignores bytes
  beyond it (newer protocol versions append fields), and copies with `memcpy`;
- for `to_` sections: `size_t encode(char *buf, Msg m)`, which sets the type and the
  length prefix and returns the bytes written;
- `name(const message_t &)`.

Differences from the m2 generators, each a defect the m2 output has:

- m2 represents a variable-length trailing field as a `void *` member, which adds 8
  bytes to the struct, so its decode copies 8 bytes beyond the message. Here the
  field is not a member; the payload follows the struct in the buffer.
- m2's decode does not check the buffer length.
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
(as `examples/itch50.tbl`'s test does), and check every record the dispatcher emits
for one message of each type.
