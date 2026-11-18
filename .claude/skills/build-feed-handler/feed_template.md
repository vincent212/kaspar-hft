# Feed description: <venue> <feed>

Every entry cites its source: document, version, page, section (or the citation
scheme of `SKILL.md`, *Specification sources*, for wikis, JSON and XML).

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|

## 2. Transport

Addresses and ports, protocols, A/B feeds, session and logon, heartbeats, throttles.

## 3. Framing

Packet header, message header, length fields (inclusive or exclusive), endianness,
alignment, batching.

## 4. Message catalogue

One table per message. Offsets are 0-based.

### <MessageName> (type `<id>`)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|

## 5. Sequencing and recovery

Sequence scope, gap rule, recovery mechanism, snapshot alignment rule, resets.

## 6. Semantics

Market data: MBO or price level, depth, update application, priority on modify,
trades, implied, trading states.
Post-trade: report types, lifecycle, identifiers, replay.

## 7. Mapping to Kaspar

By the rules in `kaspar_mapping.md`. State the priority translation, where trade
prints come from, how order and instrument identifiers are made unique, the
quantity unit, how `endOfEvent` is set, and the trading-status mapping.

| Venue message | Kaspar record or message | Notes |
|---|---|---|

## 8. Instruments

Source of definitions, symbology, tick, price and quantity scaling.

## 9. Pitfalls

One line per item of `pitfalls.md`: how it applies, or why it does not.

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|

## 11. Facts the specification does not give

What the handler or its configuration needs that is not in the documents. Each
entry says where it is used and is marked *to obtain from the venue* until the
venue or the user supplies it, with the source then.

| Fact | Used for | Value | Source |
|---|---|---|---|
| message and connection rate limits | throttles, reconnect backoff | | to obtain from the venue |
| tick size per instrument, where not in the instrument data | asset tick, OB ladder | | to obtain from the venue |
| account options and permissions (order types, short sales, algorithms) | order manager, local rejects | | to obtain from the venue |
| cancel on disconnect: on or off, per session or per account | order manager, recovery | | to obtain from the venue |
| production, backup and mirrored hosts | configuration | | to obtain from the venue |
| test-environment hosts, credentials and hours | live check | | to obtain from the venue |
