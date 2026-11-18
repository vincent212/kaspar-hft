# Feed description: <venue> <feed>

Every entry cites its source: document, version, page, section.

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
