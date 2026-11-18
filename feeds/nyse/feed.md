# Feed description: NYSE Pillar Integrated Feed

Every entry cites its source: document, version, page, section. Abbreviations:
**IF** Pillar Integrated Feed Client Specification v2.5h; **CCS** Pillar Equities Common
Client Specification v2.4r. Page numbers are the printed numbers, which equal the PDF
page numbers in both documents.

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| Pillar Integrated Feed Client Specification (NYSE, NYSE American, NYSE Arca, NYSE Texas, NYSE National Integrated Feeds) | 2.5h | July 30, 2026 | Integrated Feed messages 100-114 and 223, publication hours, control messages used, product IDs (IF p.1, p.6-27). https://www.nyse.com/publicdocs/nyse/data/NYSE_Pillar_Integrated_Feed_Client_Specification.pdf |
| Pillar Equities Common Client Specification (NYSE, NYSE American, NYSE Arca, NYSE Texas, NYSE National) | 2.4r | July 21, 2026 | Packet header, message header, data conventions, control messages 1, 2, 3, 32, 34, the Pillar Request Server (retransmission, refresh, symbol mapping refresh: 10, 11, 12, 13, 15, 31, 35), start, restart and failover behaviour, symbol mapping file (CCS p.7-33). https://www.ice.com/publicdocs/nyse/data/NYSE_Pillar_Equities_Common_Client_Specification_v2.4r.pdf |

- One Integrated Feed specification covers five markets. The title page lists NYSE
  National in addition to NYSE, American, Arca and Texas (IF p.1), and Appendix A gives
  its product ID (IF p.27). Per-market differences the spec states: publication hours
  (IF p.7, section 1.3.1); Imbalance fields and values per market, with columns for NYSE,
  American, Arca and Texas only (IF p.15-18, section 7); trade conditions marked
  "Texas only" or "Arca, American and NYSE only" (IF p.13, p.21); Late session "Non-NYSE
  only" (CCS p.17-18); MsgSize may differ per market for one type, so MsgSize must be used
  to find the next message (CCS p.9, section 3.1.1).
- Revision tables read: IF p.2-3, CCS p.2-4. Changes that affect decoding: CCS v2.4b
  made every price signed; IF v2.4 (Q1 2022) put TradeCond1-4 in messages 103 and 110 in
  place of DBExecID; IF v2.4a renamed ParitySplits to Reserved (100-104, 106); IF v2.5
  (May 2022) turned Modify byte 33 and Replace byte 40 from Reserved into Side; IF v2.5d
  replaced the significant imbalance indicator in 105 by a reserved field; CCS v2.4r added
  Late Close Eligible and ETH Eligible to message 3 (bytes 42-43). The CCS revision row
  for July 21, 2026 says "2.6r"; the cover says 2.4r (section 10).
- Other documents referenced but not read: NYSE Symbology, IP Addresses spreadsheet
  (https://www.nyse.com/publicdocs/nyse/data/IP_Addresses.xlsx), NYSE Group Pillar
  Equities Functional Differences (auction details, IF p.15-18).

## 2. Transport

- **Channels and lines:** a feed is the union of its channels; each channel is
  published on two multicast groups, line A and line B (CCS p.7, section 1.1). Addresses
  and ports are in the IP Addresses spreadsheet (CCS p.7), not in either specification.
- **Channel types** (IF p.6, section 1.2; CCS p.7): publisher (real-time) channels;
  Refresh multicast channels and Retransmission multicast channels, one-to-one with the
  real-time channels; a separate Stock Summary channel for message 223 (IF p.6, p.26).
  Messages 105 and 106 also appear on the Refresh channels (IF p.6).
- **Request Server** (CCS p.19-22, section 5): TCP, for retransmission, refresh and
  symbol mapping requests. Module A and B; a new connection drops the client's other one
  (p.21, 5.1.9 item 1). Recommended to stay connected all day (p.19, 5.1.1). Requests
  are sent in a packet with Delivery Flag 11 and "a valid sequence number" (p.22-23,
  p.29). Several requests may be outstanding; answers come in request order (p.19).
  Source IPs must be registered and sessions certified in CERT (p.19, p.31).
- **Heartbeats:** a packet header with Delivery Flag 1 and NumberMsgs 0, once a second on
  publisher, refresh and retransmission channels, once a minute from the Request Server
  (CCS p.8, section 2.2). Request Server heartbeats must be answered with a Heartbeat
  Response within 5 seconds or the connection is closed (p.21, 5.1.9 item 3; p.28-29).
- **Quotas** (CCS p.20, section 5.1.3; p.21, 5.1.9): at most 1,000 messages per
  retransmission request; 10,000 retransmission requests and 5,000 refresh requests per
  client ID per day; 500 wildcard full refreshes and 500 wildcard symbol mapping requests
  per client ID. 100 logons or session rejects per client ID per day trigger a 60-second
  lockout (p.21, 5.1.10).
- **Packet size:** at most 1400 bytes (CCS p.8, section 2.1).
- **Sites:** Mahwah; disaster recovery in Cermak with the same channels and groups and
  different source IPs, cold-started (CCS p.30, section 8.3).
- **Product IDs** (IF p.27, Appendix A): NYSE 11, NYSE American 59, NYSE National 109,
  NYSE Arca 157, NYSE Texas 209. Used in requests and in Sequence Number Reset.
- **Hours** (IF p.7, section 1.3.1; CCS p.31, section 8.4): feed start 2:00-2:04am ET by
  market; order messages NYSE Arca 8:59pm-8:00pm (overnight session, IF v2.5h), American,
  Texas, National 6:30am-8:00pm, NYSE 6:30am-4:00pm.

## 3. Framing

- **Endianness:** binary fields little-endian (CCS p.9, section 3).
- **Packet** (CCS p.8, section 2.1.1): 16-byte header (PktSize including the header,
  DeliveryFlag, NumberMsgs, SeqNum of the first message, SendTime, SendTimeNS), then
  NumberMsgs messages back to back. A message never straddles packets (CCS p.9).
- **Message header** (CCS p.9, section 3.1): MsgSize (2 bytes, the size of the message,
  counting itself), MsgType (2 bytes). The field table takes MsgSize as an inclusive
  `@length_prefix`; offsets in section 4 are the specification's minus 2, and sizes
  exclude the 2-byte prefix (spec size minus 2; each message's spec size is in its
  description). The generated structs include the prefix as `MessageLength`, so
  `sizeof` equals the spec size.
- **Message growth** (CCS p.9-10, section 3.1.1): "clients should never hard code msg
  sizes"; a message may be longer than its table (new trailing fields, or market
  variants). The generated decoder accepts a longer MsgSize and returns it as `size`.
- **Alignment:** 1-byte, no filler (CCS p.9).
- **Strings:** ASCII, left aligned, NUL padded (CCS p.9). FirmID is "blank-filled" (IF
  p.9, p.19): spaces in every sample message (section 10). Symbol is NUL terminated in
  11 bytes (CCS p.14).
- **Prices:** signed 4-byte integers, divided by 10^PriceScaleCode of the symbol (CCS
  p.11, section 3.5). No negative prices are published.
- **Timestamps** (CCS p.10, section 3.2): seconds and nanoseconds since the Unix epoch,
  UTC. Messages 100-104, 110-114 carry only SourceTimeNS; the seconds come from the last
  Source Time Reference (type 2) of the matching-engine partition. Messages 1, 32, 34,
  105, 106, 223 carry SourceTime and SourceTimeNS.
- **Shortened Refresh Header** (CCS p.25-26, section 7.4.1): message 35 has a 16-byte
  form and an 8-byte form with the same type. The table declares the 16-byte form; the
  8-byte form is the untyped `RefreshHeaderShort` (section 10).
- **Layers** in the table: `pkt` packet header (both directions) and short refresh
  header; `ctl` control messages on publisher, refresh and retransmission channels;
  `req` Request Server messages over TCP; `xdp` Integrated Feed messages. Type codes do
  not collide across `ctl` and `xdp`; a handler decodes `xdp` first and falls back to
  `ctl` on `unknown`.

## 4. Message catalogue

Generated by `fieldgen.py pillar.tbl --markdown`. Offsets are 0-based from MsgType (the
specification's offset minus 2); sizes exclude the 2-byte MsgSize prefix. `--check`
passes: 6 sections, 28 messages (`pkt::from_nyse` 2, `pkt::to_nyse` 1, `ctl::from_nyse`
7, `req::from_nyse` 1, `req::to_nyse` 4, `xdp::from_nyse` 13).

### pkt::from_nyse

#### PacketHeader (type none, 16 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| PktSize | 0 | 2 | Binary | Size of the packet in bytes, including this 16-byte header; at most 1400 (p.8, section 2.1) |
| DeliveryFlag | 2 | 1 | Binary | 1 heartbeat, 10 failover, 11 original, 12 sequence number reset, 13 only packet of a retransmission, 15 part of a retransmission, 17 only packet of a refresh, 18 start of refresh, 19 part of refresh, 20 end of refresh, 21 message unavailable |
| NumberMsgs | 3 | 1 | Binary | Messages in this packet; 0 for a heartbeat (p.8, section 2.2) |
| SeqNum | 4 | 4 | Binary | Sequence number of the first message in the packet; the others follow implicitly (p.10, section 3.3) |
| SendTime | 8 | 4 | Binary | Time the packet was published, seconds since the Unix epoch, UTC |
| SendTimeNS | 12 | 4 | Binary | Nanosecond offset from SendTime |

#### RefreshHeaderShort (type none, 8 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MsgSize | 0 | 2 | Binary | 8 (not stated; the spec says the header is 8 bytes) |
| MsgType | 2 | 2 | Binary | 35 |
| CurrentRefreshPkt | 4 | 2 | Binary | The current refresh packet for this symbol |
| TotalRefreshPkts | 6 | 2 | Binary | Total refresh packets for this symbol |

### pkt::to_nyse

#### PacketHeader (type none, 16 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| PktSize | 0 | 2 | Binary | Size of the packet in bytes, including this header |
| DeliveryFlag | 2 | 1 | Binary | 11 for every client request |
| NumberMsgs | 3 | 1 | Binary | Messages in this packet |
| SeqNum | 4 | 4 | Binary | Client's request sequence number; echoed as RequestSeqNum in the Request Response (p.28, section 7.6) |
| SendTime | 8 | 4 | Binary | Seconds since the Unix epoch, UTC |
| SendTimeNS | 12 | 4 | Binary | Nanosecond offset from SendTime |

### ctl::from_nyse

#### SequenceNumberReset (type `1` (0x1), 12 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | SequenceNumberReset: resets the channel's message sequence number to 1, at start of day or after a failure; always alone in its packet, packet SeqNum 1, DeliveryFlag 12 (10 during failover) (CCS v2.4r, p.13, section 4.1; spec Msg Size 14) |
| SourceTime | 2 | 4 | Binary | Spec offset 4. Seconds since the Unix epoch, UTC |
| SourceTimeNS | 6 | 4 | Binary | Spec offset 8. Nanosecond offset from SourceTime |
| ProductID | 10 | 1 | Binary | Spec offset 12. Feed ID: Integrated Feed NYSE 11, American 59, National 109, Arca 157, Texas 209 (IF p.27, Appendix A) |
| ChannelID | 11 | 1 | Binary | Spec offset 13. Multicast channel the packet was sent on |

#### SourceTimeReference (type `2` (0x2), 14 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | SourceTimeReference: seconds part of SourceTime for the following messages of one matching-engine partition; sent at the start of every second of activity; may share packets with market data (CCS v2.4r, p.13, section 4.2; spec Msg Size 16) |
| ID | 2 | 4 | Binary | Spec offset 4. Originating matching engine partition; linked to System ID in the Symbol Index Mapping message (p.10, section 3.2; p.13) |
| SymbolSeqNum | 6 | 4 | Binary | Spec offset 8. Reserved; ignore the content |
| SourceTime | 10 | 4 | Binary | Spec offset 12. Seconds since the Unix epoch, UTC |

#### SymbolIndexMapping (type `3` (0x3), 42 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | SymbolIndexMapping: reference data for one symbol; at startup, in a refresh, or after a failover (CCS v2.4r, p.14-15, section 4.3; spec Msg Size 44) |
| SymbolIndex | 2 | 4 | Binary | Spec offset 4. Symbol ID used by every message for this symbol |
| Symbol | 6 | 11 | ASCII | Spec offset 8. NUL-terminated symbol, NYSE symbology |
| Reserved | 17 | 1 | Binary | Spec offset 19. Reserved |
| MarketID | 18 | 2 | Binary | Spec offset 20. 1 NYSE, 3 NYSE Arca, 9 NYSE American, 10 NYSE National, 11 NYSE Texas |
| SystemID | 20 | 1 | Binary | Spec offset 22. TXN (matching engine partition) of the symbol |
| ExchangeCode | 21 | 1 | ASCII | Spec offset 23. Listing market: A NYSE American, F TXSE, L LTSE, M NYSE Texas, N NYSE, P NYSE Arca, Q NASDAQ, V IEX, Z CBOE |
| PriceScaleCode | 22 | 1 | Binary | Spec offset 24. Price = value / 10^PriceScaleCode; 6, 4 or 3 (p.11, section 3.5) |
| SecurityType | 23 | 1 | ASCII | Spec offset 25. A ADR, C common, D debenture, E ETF, F foreign, H ADS, I units, L index linked note, M other, O ordinary, P preferred, R rights, S beneficial interest, T test, U closed end fund, W warrants |
| LotSize | 24 | 2 | Binary | Spec offset 26. Round lot size in shares |
| PrevClosePrice | 26 | 4 | Price | Spec offset 28. Previous day's closing price |
| PrevCloseVolume | 30 | 4 | Binary | Spec offset 32. Previous day's closing volume |
| PriceResolution | 34 | 1 | Binary | Spec offset 36. 0 all penny, 1 penny/nickel, 5 nickel/dime |
| RoundLot | 35 | 1 | ASCII | Spec offset 37. Round lots accepted: Y or N |
| MPV | 36 | 2 | Binary | Spec offset 38. Minimum price variation in 1/100 cent (1 = $0.0001) |
| UnitOfTrade | 38 | 2 | Binary | Spec offset 40. 1, 10, 50 or 100 shares |
| LateCloseEligible | 40 | 1 | Binary | Spec offset 42. 0 not eligible, 1 eligible (added v2.4r) |
| ETHEligible | 41 | 1 | Binary | Spec offset 43. Extended trading sessions: 0 not eligible, 1 eligible (added v2.4r) |

#### MessageUnavailable (type `31` (0x1F), 12 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | MessageUnavailable: a requested retransmission range (or part) is unavailable; on the retransmission channels, DeliveryFlag 21 (CCS v2.4r, p.25, section 7.3; spec Msg Size 14) |
| BeginSeqNum | 2 | 4 | Binary | Spec offset 4. First unavailable sequence number |
| EndSeqNum | 6 | 4 | Binary | Spec offset 8. Last unavailable sequence number |
| ProductID | 10 | 1 | Binary | Spec offset 12. Feed ID |
| ChannelID | 11 | 1 | Binary | Spec offset 13. Channel ID |

#### SymbolClear (type `32` (0x20), 18 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | SymbolClear: clear all state of the symbol; an unrequested full refresh of it follows (CCS v2.4r, p.16, section 4.4; spec Msg Size 20) |
| SourceTime | 2 | 4 | Binary | Spec offset 4. Seconds since the Unix epoch, UTC |
| SourceTimeNS | 6 | 4 | Binary | Spec offset 8. Nanosecond offset from SourceTime |
| SymbolIndex | 10 | 4 | Binary | Spec offset 12. Symbol ID |
| NextSourceSeqNum | 14 | 4 | Binary | Spec offset 16. "The sequence number in the next message for this symbol" (symbol or channel sequence: section 10 of feed.md) |

#### SecurityStatus (type `34` (0x22), 44 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | SecurityStatus: halt, resume, short sale restriction, market session and price indication changes of one symbol (CCS v2.4r, p.16-18, section 4.5; spec Msg Size 46) |
| SourceTime | 2 | 4 | Binary | Spec offset 4. Seconds since the Unix epoch, UTC |
| SourceTimeNS | 6 | 4 | Binary | Spec offset 8. Nanosecond offset from SourceTime |
| SymbolIndex | 10 | 4 | Binary | Spec offset 12. Symbol ID |
| SymbolSeqNum | 14 | 4 | Binary | Spec offset 16. Symbol sequence number |
| SecurityStatus | 18 | 1 | ASCII | Spec offset 20. Halt 4 halt, 5 resume, 6 suspend/operational halt; SSR A activated, C continued, D deactivated; session P pre-opening, B begin accepting orders, E early, O core, L late (non-NYSE), X closed; indication I halt resume, G pre-opening |
| HaltCondition | 19 | 1 | ASCII | Spec offset 21. ~ not halted, D news released, I order imbalance, P news pending, M LULD pause, X equipment changeover, A SIP outage or extraordinary activity, C regulatory, E merger effective, F ETF IIV/components n/a, N corporate action, O new security offering, V listing exchange discretionary, 6 suspend/operational halt, 1-3 MWCB level |
| Reserved | 20 | 4 | Binary | Spec offset 22. Ignore the content |
| Price1 | 24 | 4 | Price | Spec offset 26. SSR triggering trade price (status A, listed here) or indication low (G, I); else 0 |
| Price2 | 28 | 4 | Price | Spec offset 30. Indication high (G, I); else 0 |
| SSRTriggeringExchangeID | 32 | 1 | ASCII | Spec offset 34. Only for status A on the listing exchange; else 0x20 |
| SSRTriggeringVolume | 33 | 4 | Binary | Spec offset 35. Only for status A on the listing exchange; else 0 |
| Time | 37 | 4 | Binary | Spec offset 39. HHMMSSmmm; SSR trigger time for status A; else 0 |
| SSRState | 41 | 1 | ASCII | Spec offset 43. ~ no SSR, E SSR in effect |
| MarketState | 42 | 1 | ASCII | Spec offset 44. P pre-opening, E early, O core, L late (non-NYSE), X closed |
| SessionState | 43 | 1 | ASCII | Spec offset 45. Unused, 0x00 |

#### RefreshHeader (type `35` (0x23), 14 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | RefreshHeader: first message of every refresh packet; this full 16-byte form is in the first packet for each symbol, later packets have the 8-byte form (pkt::from_nyse RefreshHeaderShort). Not used in Symbol Index Mapping refreshes (CCS v2.4r, p.25, section 7.4; p.21, section 5.1.8; spec Msg Size 16) |
| CurrentRefreshPkt | 2 | 2 | Binary | Spec offset 4. The current refresh packet for this symbol |
| TotalRefreshPkts | 4 | 2 | Binary | Spec offset 6. Total refresh packets for this symbol |
| LastSeqNum | 6 | 4 | Binary | Spec offset 8. Last channel sequence number; the refresh is the book as of this number |
| LastSymbolSeqNum | 10 | 4 | Binary | Spec offset 12. Last symbol sequence number; the refresh is the symbol state as of this number |

### req::from_nyse

#### RequestResponse (type `11` (0xB), 27 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | RequestResponse: Request Server's reply over TCP to a retransmission, refresh or symbol index mapping request (CCS v2.4r, p.28, section 7.6; spec Msg Size 29) |
| RequestSeqNum | 2 | 4 | Binary | Spec offset 4. Sequence number of the client's request |
| BeginSeqNum | 6 | 4 | Binary | Spec offset 8. Retransmission: first requested; refresh and mapping requests: 0 |
| EndSeqNum | 10 | 4 | Binary | Spec offset 12. Retransmission: last requested; refresh and mapping requests: 0 |
| SourceID | 14 | 10 | ASCII | Spec offset 16. Client ID, NUL padded |
| ProductID | 24 | 1 | Binary | Spec offset 26. Feed ID |
| ChannelID | 25 | 1 | Binary | Spec offset 27. Channel ID |
| Status | 26 | 1 | ASCII | Spec offset 28. '0' accepted, '1' invalid Source ID, '3' sequence range too large, '4' daily request limit, '5' daily refresh limit, '6' request SeqNum too old, '7' invalid channel, '8' invalid product, '9' invalid MsgType or MsgType/MsgSize mismatch |

### req::to_nyse

#### RetransmissionRequest (type `10` (0xA), 22 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | RetransmissionRequest: re-publish a range of messages on the retransmission channel; at most 1,000 messages (CCS v2.4r, p.22, section 6.1; p.20, section 5.1.3; spec Msg Size 24) |
| BeginSeqNum | 2 | 4 | Binary | Spec offset 4. First sequence number requested |
| EndSeqNum | 6 | 4 | Binary | Spec offset 8. Last sequence number requested |
| SourceID | 10 | 10 | ASCII | Spec offset 12. Client ID; trailing characters NUL (p.21, section 5.1.9 item 6) |
| ProductID | 20 | 1 | Binary | Spec offset 22. Feed ID (IF p.27, Appendix A) |
| ChannelID | 21 | 1 | Binary | Spec offset 23. Channel on which the gap occurred |

#### HeartbeatResponse (type `12` (0xC), 12 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | HeartbeatResponse: reply to a Request Server heartbeat within 5 seconds (CCS v2.4r, p.28-29, section 7.7; p.21, section 5.1.9 item 3; spec Msg Size 14) |
| SourceID | 2 | 10 | ASCII | Spec offset 4. Client ID; trailing characters NUL |

#### SymbolIndexMappingRequest (type `13` (0xD), 19 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | SymbolIndexMappingRequest: re-publish Symbol Index Mapping messages for one symbol or all on the refresh channel (CCS v2.4r, p.23, section 7.1; spec Msg Size 21) |
| SymbolIndex | 2 | 4 | Binary | Spec offset 4. Symbol ID; 0 for all symbols of the channel |
| SourceID | 6 | 10 | ASCII | Spec offset 8. Client ID; trailing characters NUL |
| ProductID | 16 | 1 | Binary | Spec offset 18. Feed ID |
| ChannelID | 17 | 1 | Binary | Spec offset 19. Channel ID |
| RetransmitMethod | 18 | 1 | Binary | Spec offset 20. 0 deliver via UDP |

#### RefreshRequest (type `15` (0xF), 18 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | RefreshRequest: snapshot of one symbol or all symbols of a channel on the refresh channel (CCS v2.4r, p.23-24, section 7.2; spec Msg Size 20) |
| SymbolIndex | 2 | 4 | Binary | Spec offset 4. Symbol ID; 0 for all symbols of the channel |
| SourceID | 6 | 10 | ASCII | Spec offset 8. Client ID; trailing characters NUL |
| ProductID | 16 | 1 | Binary | Spec offset 18. Feed ID |
| ChannelID | 17 | 1 | Binary | Spec offset 19. Channel ID |

### xdp::from_nyse

#### AddOrder (type `100` (0x64), 37 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | AddOrder: a new visible order added to the book; may be sent again with the same OrderID when a routed order returns unexecuted (IF v2.5h, p.9, section 2; spec Msg Size 39) |
| SourceTimeNS | 2 | 4 | Binary | Spec offset 4. Nanoseconds within the second of the last Source Time Reference |
| SymbolIndex | 6 | 4 | Binary | Spec offset 8. Symbol ID |
| SymbolSeqNum | 10 | 4 | Binary | Spec offset 12. Symbol sequence number |
| OrderID | 14 | 8 | Binary | Spec offset 16. Order ID, unique, current day only |
| Price | 22 | 4 | Price | Spec offset 24. Order price; scale by PriceScaleCode |
| Volume | 26 | 4 | Binary | Spec offset 28. Order quantity in shares |
| Side | 30 | 1 | ASCII | Spec offset 32. B buy, S sell |
| FirmID | 31 | 5 | ASCII | Spec offset 33. Firm ID; "blank-filled" when not attributed |
| Reserved1 | 36 | 1 | Binary | Spec offset 38. 0 |

#### ModifyOrder (type `101` (0x65), 33 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | ModifyOrder: new price and/or volume of an order, for a reason other than cancel/replace or execution (IF v2.5h, p.10, section 3; spec Msg Size 35) |
| SourceTimeNS | 2 | 4 | Binary | Spec offset 4. Nanoseconds within the reference second |
| SymbolIndex | 6 | 4 | Binary | Spec offset 8. Symbol ID |
| SymbolSeqNum | 10 | 4 | Binary | Spec offset 12. Symbol sequence number |
| OrderID | 14 | 8 | Binary | Spec offset 16. Order modified |
| Price | 22 | 4 | Price | Spec offset 24. New price |
| Volume | 26 | 4 | Binary | Spec offset 28. New quantity in shares |
| PositionChange | 30 | 1 | Binary | Spec offset 32. "Currently defaulted to 0"; '0' kept, '1' lost position; position kept if the price is unchanged, lost if it changed |
| Side | 31 | 1 | ASCII | Spec offset 33. B buy, S sell (was Reserved 1 before v2.5) |
| Reserved2 | 32 | 1 | Binary | Spec offset 34. 0 |

#### DeleteOrder (type `102` (0x66), 23 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | DeleteOrder: order removed for any reason other than full execution or replace (IF v2.5h, p.11, section 4; spec Msg Size 25) |
| SourceTimeNS | 2 | 4 | Binary | Spec offset 4. Nanoseconds within the reference second |
| SymbolIndex | 6 | 4 | Binary | Spec offset 8. Symbol ID |
| SymbolSeqNum | 10 | 4 | Binary | Spec offset 12. Symbol sequence number |
| OrderID | 14 | 8 | Binary | Spec offset 16. Order deleted |
| Reserved1 | 22 | 1 | Binary | Spec offset 24. 0 |

#### OrderExecution (type `103` (0x67), 40 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | OrderExecution: a displayed order executed, partly or fully; the order leaves the book when Volume equals its remaining shares (IF v2.5h, p.12-13, section 5; spec Msg Size 42) |
| SourceTimeNS | 2 | 4 | Binary | Spec offset 4. Nanoseconds within the reference second |
| SymbolIndex | 6 | 4 | Binary | Spec offset 8. Symbol ID |
| SymbolSeqNum | 10 | 4 | Binary | Spec offset 12. Symbol sequence number |
| OrderID | 14 | 8 | Binary | Spec offset 16. Order executed |
| TradeID | 22 | 4 | Binary | Spec offset 24. Execution ID, referenced by Trade Cancel |
| Price | 26 | 4 | Price | Spec offset 28. Execution price; may differ from the order's price |
| Volume | 30 | 4 | Binary | Spec offset 32. Executed shares |
| PrintableFlag | 34 | 1 | Binary | Spec offset 36. 0 not printed to the SIP, 1 printed; 0 for auction executions |
| Reserved1 | 35 | 1 | Binary | Spec offset 37. 0 |
| TradeCond1 | 36 | 1 | ASCII | Spec offset 38. @ regular sale, C cash (Texas only) |
| TradeCond2 | 37 | 1 | ASCII | Spec offset 39. space n/a, F ISO, O opening, 5 reopening, 6 closing (Arca, American, NYSE), 7 QCT (Texas) |
| TradeCond3 | 38 | 1 | ASCII | Spec offset 40. space n/a, T extended hours, U extended hours sold (out of sequence), Z sold |
| TradeCond4 | 39 | 1 | ASCII | Spec offset 41. space n/a, I odd lot, V contingent (Texas) |

#### ReplaceOrder (type `104` (0x68), 40 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | ReplaceOrder: cancel/replace; the order is removed and a new order with a new OrderID, same symbol, side and attribution is added (IF v2.5h, p.14, section 6; spec Msg Size 42) |
| SourceTimeNS | 2 | 4 | Binary | Spec offset 4. Nanoseconds within the reference second |
| SymbolIndex | 6 | 4 | Binary | Spec offset 8. Symbol ID |
| SymbolSeqNum | 10 | 4 | Binary | Spec offset 12. Symbol sequence number |
| OrderID | 14 | 8 | Binary | Spec offset 16. Order replaced |
| NewOrderID | 22 | 8 | Binary | Spec offset 24. Order ID of the replacement |
| Price | 30 | 4 | Price | Spec offset 32. New price |
| Volume | 34 | 4 | Binary | Spec offset 36. New quantity in shares |
| Side | 38 | 1 | ASCII | Spec offset 40. B buy, S sell (was Reserved 1 before v2.5) |
| Reserved2 | 39 | 1 | Binary | Spec offset 41. 0 |

#### Imbalance (type `105` (0x69), 71 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | Imbalance: auction imbalance, at most once a second during auctions, only when a value changed; also in refreshes. Fields marked No for a market carry defaults there (IF v2.5h, p.15-18, section 7; spec Msg Size 73) |
| SourceTime | 2 | 4 | Binary | Spec offset 4. Seconds since the Unix epoch, UTC |
| SourceTimeNS | 6 | 4 | Binary | Spec offset 8. Nanosecond offset from SourceTime |
| SymbolIndex | 10 | 4 | Binary | Spec offset 12. Symbol ID |
| SymbolSeqNum | 14 | 4 | Binary | Spec offset 16. Symbol sequence number |
| ReferencePrice | 18 | 4 | Price | Spec offset 20. Price at which imbalances are calculated |
| PairedQty | 22 | 4 | Binary | Spec offset 24. Shares paired at the reference price |
| TotalImbalanceQty | 26 | 4 | Binary | Spec offset 28. Total imbalance at the reference price |
| MarketImbalanceQty | 30 | 4 | Binary | Spec offset 32. Market order imbalance; not NYSE |
| AuctionTime | 34 | 2 | Binary | Spec offset 36. Projected auction time, hhmm |
| AuctionType | 36 | 1 | ASCII | Spec offset 38. O early opening (not NYSE), M core opening, H reopening, C closing, P extreme closing imbalance (NYSE), R significant closing imbalance (NYSE) |
| ImbalanceSide | 37 | 1 | ASCII | Spec offset 39. B buy, S sell, space none |
| ContinuousBookClearingPrice | 38 | 4 | Price | Spec offset 40. Price nearest the reference price with zero imbalance |
| AuctionInterestClearingPrice | 42 | 4 | Price | Spec offset 44. Price at which auction-only interest would trade |
| SSRFilingPrice | 46 | 4 | Price | Spec offset 48. NYSE only: Sell Short filing price under SSR |
| IndicativeMatchPrice | 50 | 4 | Price | Spec offset 52. Not NYSE: best price for maximum executable volume within the collars |
| UpperCollar | 54 | 4 | Price | Spec offset 56. Not NYSE |
| LowerCollar | 58 | 4 | Price | Spec offset 60. Not NYSE |
| AuctionStatus | 62 | 1 | Binary | Spec offset 64. 0 will run, 1 will run (interest within collars), 2 will not run (imbalance through collars), 3 will not run, moves to the close; NYSE 0 and 3 only |
| FreezeStatus | 63 | 1 | Binary | Spec offset 65. 0 no freeze, 1 imbalance freeze in effect |
| NumExtensions | 64 | 1 | Binary | Spec offset 66. Times a halt period was extended; not NYSE |
| UnpairedQty | 65 | 4 | Binary | Spec offset 67. NYSE only: unpaired shares at or better than the reference price |
| UnpairedSide | 69 | 1 | ASCII | Spec offset 71. B, S, space; NYSE only |
| Reserved | 70 | 1 | ASCII | Spec offset 72. Reserved |

#### AddOrderRefresh (type `106` (0x6A), 41 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | AddOrderRefresh: one resting order of a symbol in a refresh (requested, on the refresh channel; or unrequested after Symbol Clear) (IF v2.5h, p.19, section 8; spec Msg Size 43) |
| SourceTime | 2 | 4 | Binary | Spec offset 4. Seconds since the Unix epoch, UTC |
| SourceTimeNS | 6 | 4 | Binary | Spec offset 8. Nanosecond offset from SourceTime |
| SymbolIndex | 10 | 4 | Binary | Spec offset 12. Symbol ID |
| SymbolSeqNum | 14 | 4 | Binary | Spec offset 16. Symbol sequence number |
| OrderID | 18 | 8 | Binary | Spec offset 20. Order ID |
| Price | 26 | 4 | Price | Spec offset 28. Order price |
| Volume | 30 | 4 | Binary | Spec offset 32. Order quantity in shares |
| Side | 34 | 1 | ASCII | Spec offset 36. B buy, S sell |
| FirmID | 35 | 5 | ASCII | Spec offset 37. Firm ID, or blanks |
| Reserved1 | 40 | 1 | Binary | Spec offset 42. 0 |

#### NonDisplayedTrade (type `110` (0x6E), 31 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | NonDisplayedTrade: a match between two non-displayed orders; no book change (IF v2.5h, p.20-21, section 9; spec Msg Size 33) |
| SourceTimeNS | 2 | 4 | Binary | Spec offset 4. Nanoseconds within the reference second |
| SymbolIndex | 6 | 4 | Binary | Spec offset 8. Symbol ID |
| SymbolSeqNum | 10 | 4 | Binary | Spec offset 12. Symbol sequence number |
| TradeID | 14 | 4 | Binary | Spec offset 16. Trade ID, referenced by Trade Cancel |
| Price | 18 | 4 | Price | Spec offset 20. Execution price |
| Volume | 22 | 4 | Binary | Spec offset 24. Shares |
| PrintableFlag | 26 | 1 | Binary | Spec offset 28. 0 not printed to the SIP, 1 printed |
| TradeCond1 | 27 | 1 | ASCII | Spec offset 29. @ regular sale, C cash (Texas only) |
| TradeCond2 | 28 | 1 | ASCII | Spec offset 30. space, F ISO, O opening, 5 reopening, 6 closing, 7 QCT (Texas) |
| TradeCond3 | 29 | 1 | ASCII | Spec offset 31. space, T extended hours, U extended hours sold, Z sold |
| TradeCond4 | 30 | 1 | ASCII | Spec offset 32. space, I odd lot, V contingent (Texas) |

#### CrossTrade (type `111` (0x6F), 27 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | CrossTrade: bulk volume of a completed crossing auction; each participating order is also reported by a non-printable Order Execution or Non-Displayed Trade (IF v2.5h, p.22, section 10; spec Msg Size 29) |
| SourceTimeNS | 2 | 4 | Binary | Spec offset 4. Nanoseconds within the reference second |
| SymbolIndex | 6 | 4 | Binary | Spec offset 8. Symbol ID |
| SymbolSeqNum | 10 | 4 | Binary | Spec offset 12. Symbol sequence number |
| CrossID | 14 | 4 | Binary | Spec offset 16. Cross ID, referenced by Cross Correction |
| Price | 18 | 4 | Price | Spec offset 20. Auction price |
| Volume | 22 | 4 | Binary | Spec offset 24. Shares executed in the auction |
| CrossType | 26 | 1 | ASCII | Spec offset 28. E early opening, O opening, 5 reopening, 6 closing auction |

#### TradeCancel (type `112` (0x70), 18 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | TradeCancel: an earlier Order Execution or Non-Displayed Trade is cancelled; no book change (IF v2.5h, p.23, section 11; spec Msg Size 20) |
| SourceTimeNS | 2 | 4 | Binary | Spec offset 4. Nanoseconds within the reference second |
| SymbolIndex | 6 | 4 | Binary | Spec offset 8. Symbol ID |
| SymbolSeqNum | 10 | 4 | Binary | Spec offset 12. Symbol sequence number |
| TradeID | 14 | 4 | Binary | Spec offset 16. TradeID of the execution cancelled |

#### CrossCorrection (type `113` (0x71), 22 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | CrossCorrection: corrected volume of an earlier Cross Trade (IF v2.5h, p.24, section 12; spec Msg Size 24) |
| SourceTimeNS | 2 | 4 | Binary | Spec offset 4. Nanoseconds within the reference second |
| SymbolIndex | 6 | 4 | Binary | Spec offset 8. Symbol ID |
| SymbolSeqNum | 10 | 4 | Binary | Spec offset 12. Symbol sequence number |
| CrossID | 14 | 4 | Binary | Spec offset 16. CrossID of the Cross Trade corrected |
| Volume | 18 | 4 | Binary | Spec offset 20. Corrected volume |

#### RetailPriceImprovement (type `114` (0x72), 15 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | RetailPriceImprovement: sides on which hidden RPI interest exists between the best bid and offer (IF v2.5h, p.25, section 13; spec Msg Size 17) |
| SourceTimeNS | 2 | 4 | Binary | Spec offset 4. Nanoseconds within the reference second |
| SymbolIndex | 6 | 4 | Binary | Spec offset 8. Symbol ID |
| SymbolSeqNum | 10 | 4 | Binary | Spec offset 12. Symbol sequence number |
| RPIIndicator | 14 | 1 | ASCII | Spec offset 16. space none, A bid, B offer, C both |

#### StockSummary (type `223` (0xDF), 34 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | StockSummary: daily high, low, open, close and volume of a symbol, every 60 seconds on the separate Stock Summary channel (IF v2.5h, p.26, section 14; spec Msg Size 36) |
| SourceTime | 2 | 4 | Binary | Spec offset 4. Seconds since the Unix epoch, UTC |
| SourceTimeNS | 6 | 4 | Binary | Spec offset 8. Nanosecond offset from SourceTime |
| SymbolIndex | 10 | 4 | Binary | Spec offset 12. Symbol ID |
| HighPrice | 14 | 4 | Price | Spec offset 16. Exchange high price of the day |
| LowPrice | 18 | 4 | Price | Spec offset 20. Exchange low price of the day |
| Open | 22 | 4 | Price | Spec offset 24. Exchange opening price |
| Close | 26 | 4 | Price | Spec offset 28. Exchange closing price |
| TotalVolume | 30 | 4 | Binary | Spec offset 32. Exchange cumulative volume of the day |


## 5. Sequencing and recovery

- **Scope:** one sequence per channel; channel ID and sequence number identify a message
  across the feed (CCS p.10-11, section 3.3). The packet header gives the first message's
  number; the others are numbered implicitly.
- **Symbol sequence:** most messages also carry SymbolSeqNum, the message's position among
  all messages of that symbol (CCS p.11, section 3.4). In the samples, Security Status
  (34) shares the symbol sequence with the order messages: counting it, no symbol gaps
  occur where the channel sequence has none. A client following few symbols may track
  only SymbolSeqNum and refresh a symbol on a gap (CCS p.11).
- **Gap rule:** expected = SeqNum + NumberMsgs of the previous data packet; above it is a
  gap, below it a duplicate (the other line or a retransmission). Heartbeats do not
  increment the sequence (CCS p.8). In the samples a heartbeat's SeqNum equals the next
  expected sequence number (1,645 of 1,645 and 14,284 of 14,284 heartbeats), so a
  heartbeat also reveals a gap; the spec does not state this (section 10).
- **A/B:** process both lines; fill a one-line gap from the other line (CCS p.19,
  section 5.1.2).
- **Retransmission** (CCS p.19-22, sections 5.1.2-5.1.4, 6.1): Retransmission Request (10)
  over TCP with Source ID, Product ID, Channel ID, Begin/End SeqNum, at most 1,000
  messages; Request Response (11) over TCP; the messages are re-sent on the
  Retransmission multicast channel with their original sequence numbers, possibly
  packetized differently, Delivery Flag 13 (only packet) or 15 (part). Requests may start
  from sequence number 1 (5.1.9 item 2). Numbers beyond the last processed one are
  answered with Message Unavailable (31, Delivery Flag 21), not a reject (5.1.9 item 5).
- **Refresh** (CCS p.20-21, section 5.1.5): subscribe to the publisher channel and buffer;
  connect to the Request Server; subscribe to the Refresh channel; send Refresh Request
  (15) with SymbolIndex (0 = all symbols of the channel). Each refresh packet starts with
  a Refresh Header (35); per symbol: Symbol Index Mapping (3), Imbalance (105) if one is
  current, Security Status (34), Add Order Refresh (106) per resting order (CCS p.26,
  section 7.4.2). Delivery flags: all symbols 18 / 19 / 20 by symbol; one symbol 19 for
  every packet of a multi-packet response, 17 for one packet (CCS p.26-27, 7.4.3).
  Completion is CurrentRefreshPkt = TotalRefreshPkts for each symbol. No retransmission
  of refresh channels; re-request on loss (CCS p.21, 5.1.6).
- **Snapshot alignment:** the full Refresh Header gives LastSeqNum ("the refresh is the
  state of the order book as of this sequence number") and LastSymbolSeqNum (the symbol
  state as of this symbol sequence number) (CCS p.25). Buffered messages with numbers
  "lower than" LastSeqNum are discarded (p.20, 5.1.5); messages "higher than" LastSeqNum
  are applied, per symbol, because LastSeqNum differs per symbol in an all-symbol refresh
  (p.21, 5.1.9 item 7). The refresh sample has 12 distinct LastSeqNum values in 33,935
  symbol headers. Reading taken (section 10): per symbol, a buffered message is in the
  refresh when its SymbolSeqNum <= LastSymbolSeqNum (equivalently channel SeqNum <=
  that symbol's LastSeqNum); later ones are applied.
- **Symbol Index Mapping refresh** (CCS p.21, 5.1.7-5.1.8): request 13; mapping messages
  on the Refresh channel with refresh delivery flags and no Refresh Header (fewer than 32
  symbols: only flag 20, CCS p.26). Also available as a daily file (section 8).
- **Sequence resets** (CCS p.13, section 4.1; p.30, sections 8.1-8.2): Sequence Number
  Reset (1) alone in a packet with SeqNum 1, Delivery Flag 12 at start of day, 10 during a
  publisher failover. Start of day: priming heartbeats or resets with SeqNum 1 for several
  seconds, the reset, then per symbol Symbol Index Mapping, Symbol Clear and Security
  Status. Failover: priming, reset, then per symbol mapping, Symbol Clear, last Security
  Status, all refresh messages and the last Source Time Reference, all with Delivery Flag
  10 until every symbol is refreshed, then 11.
- **Symbol Clear** (32): clear all state of the symbol; an unrequested full refresh of it
  follows (CCS p.16, section 4.4; IF p.19). NextSourceSeqNum is the next SymbolSeqNum of
  the symbol: in the sample with a startup spin, 7,638 of 7,638 Symbol Clears were
  followed by a message for the symbol with exactly that SymbolSeqNum.
- **State to clear:** on Sequence Number Reset or failover: the channel's books, the
  order-ID map, the trade-ID map, per-symbol sequence numbers, Source Time Reference
  seconds. On Symbol Clear: that symbol's orders and status. On Security Status X: that
  symbol's orders (no Delete messages are sent, IF p.11).

## 6. Semantics

- **Book type:** order by order, displayed orders only, full depth (IF p.6, section 1.1;
  p.9). Non-displayed interest appears only as Non-Displayed Trades and, for RPI, as an
  indicator (IF p.20, p.25).
- **Add Order (100):** new visible order. An order that was displayed, routed away and
  returns unexecuted with no residual is added again with the same Order ID (IF p.9).
  Order IDs are unique and valid for the current day (IF p.9; CCS p.11, 3.6.1).
- **Modify Order (101):** new price and new volume (the values after the change), for a
  reason other than cancel/replace or execution (IF p.10). Priority: kept if the price did
  not change, lost if it changed; the PositionChange field is "currently defaulted to 0"
  (IF p.10) and was 0x00 in every sample message, including 24,845 modifies that changed
  price, so it cannot be used. In one hour of NYSE data, 24,545 same-price modifies: 24,541
  reduced the volume, 4 increased it (section 10).
- **Delete Order (102):** removal for any reason except full execution or replace (IF
  p.11). Before a session change (e.g. Early to Core) orders entered for the current, or
  current and previous, sessions are deleted explicitly. At the close of trading in a
  symbol, Security Status X is sent and orders are cancelled without Delete messages.
- **Replace Order (104):** the resting order is removed and replaced by a new order with a
  new Order ID, same symbol, side and attribution, the given price and volume (IF p.14).
- **Order Execution (103):** a displayed resting order executed for Volume shares. If
  Volume equals the remaining shares the order is gone; otherwise it stays with the
  remainder at its original price, even when the execution price differs (IF p.12).
  Book and trade are one message. The side is that of the resting order; the aggressor is
  not reported. Execution price differed from the order's price in 242 of 8,576
  executions in one hour of NYSE data.
- **Non-Displayed Trade (110):** match between two non-displayed orders; no book change;
  needed for complete trade records (IF p.20). A MOC against a displayed order is
  reported only by an Order Execution.
- **Auctions:** Cross Trade (111) gives the bulk auction volume; every participating
  order is also reported by a non-printable (PrintableFlag 0) Order Execution or
  Non-Displayed Trade, "so that the auction volume is not double counted" (IF p.12, p.22).
  Cross Correction (113) corrects the volume of a Cross Trade (IF p.24).
- **Trade Cancel (112):** an earlier 103 or 110 cancelled by TradeID; no book effect (IF
  p.23). TradeID is unique per matching-engine partition, so per symbol (CCS p.11).
- **Imbalance (105):** once a second during auctions when a calculated field changes
  (IF p.15); fields per market (IF p.15-18); also part of a refresh when an imbalance is
  current (CCS p.26). Calculation at the indicative match price moved to the reference
  price on 11/10/25 (American), 11/13/25 (Arca), 11/20/25 (Texas) (IF p.18).
- **Retail Price Improvement (114):** sides where hidden RPI interest exists; space when
  none (IF p.25).
- **Stock Summary (223):** every 60 seconds per symbol, separate channel; no
  SymbolSeqNum (IF p.26).
- **Trading states** (CCS p.16-18, section 4.5): Security Status carries halt (4, 5, 6),
  SSR (A, C, D), market session (P, B, E, O, L, X) and price indication (I, G) codes;
  Halt Condition ~ when not halted; MarketState and SSRState hold the current state.
  B (begin accepting orders) is sent with MarketState still P. Whether a crossed or locked
  book is possible in any state is not stated.
- **Implied prices, overlays, icebergs:** none; reserve quantity is not displayed.
- **Events:** no event or end-of-transaction flag. Messages of one matching-engine event
  are not grouped by the spec.

## 7. Mapping to Kaspar

Records are `bfile::l3_t` (`chutil/include/bfile/r_l3.hpp`), emitted the way
`mdp3::handler_if` emits them (`mdp3/include/mdp3/handler_if.hpp`: MBO
`MDIncrementalRefreshBook` line 651; MBO trade `MDIncrementalRefreshTradeSummary` 861;
snapshot `SnapshotFullRefreshOrderBook` 925; `SecurityStatus` 1074; `Gap` 1129;
`ChannelReset` 1158, which also clears the orderID map (#122); `EndOfPacket` 1262).
`orderUpdateAction` 0 new, 1 change, 2 delete; `side` '0' bid, '1' ask; `pxd` the
price as a double; `transactTime` the full source time (Source Time Reference seconds ×
10^9 + SourceTimeNS); `sendingTime` the packet SendTime/SendTimeNS. NYSE is order by
order, so `sim::Mbp2Mbo` (`sim/src/Mbp2Mbo.cpp`, price-level to MBO) is not used. No
`en::x` venue code exists for any NYSE market (`chutil/gencode/enum/def/exch_code.enum`;
codes are appended at the end); one per market or one for all five is a decision for the
handler.

| Venue message | Kaspar record or message | Notes |
|---|---|---|
| packet end | `l3_eop_t` (msgSeqNum = SeqNum, sendingTime) | To the recorder, as `EndOfPacket`. 32-bit SeqNum fits. |
| sequence gap (section 5) | `l3_gap_v2_t` to every book of the channel | As `handler_if::Gap`, then retransmission or refresh. |
| Sequence Number Reset (1); Delivery Flag 10 failover | `l3_chr_v2_t` to every book of the channel; clear order-ID, trade-ID, symbol-sequence and source-time state | As `handler_if::ChannelReset`. The failover refresh that follows is applied as recovery records. |
| Source Time Reference (2) | none; handler state | Seconds per partition ID; joined to symbols through Symbol Index Mapping System ID (samples: ID 2 with System ID 2). |
| Symbol Index Mapping (3) | instrument loader, not a record | SymbolIndex → Kaspar securityID; PriceScaleCode, MPV, LotSize, SystemID per symbol. A later mapping overrides earlier field values but not retroactively (CCS p.12). |
| Symbol Clear (32) | `l3_mbo_v2_t` action 2 for every order of the symbol held by the handler | `l3_chr_v2_t` has no securityID and clears every book of the channel. |
| Security Status (34) | `l3_sst_t` (securityID; tradingStatus, haltReason, tradingEvent mapped) to that symbol's book | OB matches only when `tradingStatus == 17` (`frame_kaspr/include/frame/ob/act/OB.hpp:456`, the MDP3 code). Map "not halted (HaltCondition ~) and MarketState in E, O, L" to 17, everything else to a non-matching value; keep the NYSE codes in `haltReason`/`tradingEvent`. On X also delete every order of the symbol (IF p.11). |
| Add Order (100) | `l3_mbo_v2_t` action 0 | side B→'0', S→'1'; pxd = Price / 10^PriceScaleCode; displayQty = Volume; orderID = OrderID; record OrderID → (securityID, side, price, volume). If the OrderID is live (routed order back, IF p.9), send action 2 then 0 (section 10). |
| Modify Order (101), price changed | `l3_mbo_v2_t` action 1 | OB treats a Change with a new price as cancel and add (`frame_kaspr/src/OB.cpp:2430`): priority lost, as IF p.10 says. Side from the order map (byte 33 is 0 before v2.5). |
| Modify Order (101), same price, volume down | `l3_mbo_v2_t` action 1 | OB keeps priority, as IF p.10 says. |
| Modify Order (101), same price, volume up | `l3_mbo_v2_t` action 1 | IF p.10 says the position is kept; OB turns a size-up Change into cancel and add and loses it (`OB.cpp:2430`). Section 10. |
| Delete Order (102) | `l3_mbo_v2_t` action 2 | Erase from the order map. |
| Replace Order (104) | `l3_mbo_v2_t` action 2 (OrderID), then action 0 (NewOrderID) | New order, new priority. |
| Order Execution (103) | `l3_mbo_trd_v2_t` (orderID, lastQty = Volume), then `l3_mbo_v2_t` action 1 with the remainder, or action 2 at 0 | Trade before book record, as CME and `frame_kaspr/include/frame/ob/FillSlate.hpp` expect. The trade record has no price, PrintableFlag, TradeID or conditions; OB prices the fill at the book price, not the execution price (section 10). |
| Non-Displayed Trade (110) | `l3_mbp_trd_t` (securityID, pxd, sz) or none | No order ID, so `l3_mbo_trd_v2_t` cannot route it. OB ignores `l3_mbp_trd_t` unless built with `USEMBP`, and then only adds volume (`OB.cpp:1884`). |
| Cross Trade (111), Cross Correction (113) | none | Bulk volume; the per-order executions already carry it. |
| Trade Cancel (112) | none | No Kaspar record for a bust. Log. |
| Imbalance (105) | none | No imbalance record in `r_l3.hpp`. |
| Retail Price Improvement (114), Stock Summary (223) | none | No book effect. |
| Refresh Header (35), short form | handler state | Per-symbol LastSeqNum / LastSymbolSeqNum for alignment (section 5). |
| Add Order Refresh (106) | `l3_mbo_v2_t` action 0, `recovery = true` | As `SnapshotFullRefreshOrderBook`. Preceded per symbol by deleting the handler's orders of that symbol. |
| Message Unavailable (31) | `l3_gap_v2_t`; then refresh | The range cannot be recovered by retransmission. |
| events | `endOfEvent` | No event flag on the feed (section 10). |

## 8. Instruments

- **Identifier:** SymbolIndex (4 bytes). Same for a symbol every day and across all NYSE
  equity markets (CCS p.12, section 3.6.3), but the message table says it "cannot be used
  to cross reference a security between markets" (CCS p.14) (section 10).
- **Definitions:** Symbol Index Mapping (3) at startup in each channel for that channel's
  symbols, in refreshes and after failover (CCS p.12, p.14); on request (13); and as a
  daily pipe-delimited file `$Market$SymbolMapping_YYYYMMDD.txt` / `.xml`, available by
  midnight ET, at https://ftp.nyse.com/{NYSE,American,ARCA,Texas,National}SymbolMapping/
  (CCS p.32-33, section 9). The file adds CQS symbol and tape.
- **Symbology:** NYSE symbology, NUL-terminated, up to 10 characters plus NUL (CCS p.14).
- **Price:** value / 10^PriceScaleCode, PriceScaleCode 6, 4 or 3 per symbol, set nightly
  from the closing price (< $500 → 6; ≥ $500 → 4; ≥ $100,000 → 3) (CCS p.11, 3.5.1). In
  the startup sample: 7,397 symbols scale 6, 120 scale 4, 121 scale 3.
- **Tick:** MPV in 1/100 cent (1 = $0.0001; 500 = $0.05 for some symbols) (CCS p.15);
  Price Resolution 0 all penny, 1 penny/nickel, 5 nickel/dime. Ticks by price (sub-penny
  below $1) are not stated.
- **Quantity:** shares. LotSize (round lot) and Unit of Trade (1, 10, 50, 100) per symbol
  (CCS p.15). Odd-lot trades are flagged TradeCond4 I (IF p.13, p.21).
- **Channel assignment:** not in either document (IP Addresses spreadsheet).
- **Lifecycle:** Security Type T marks test symbols (CCS p.15); Order and Trade IDs valid
  for the current trading day only (CCS p.11).

## 9. Pitfalls

**1. Reading the specification**

- Several documents: IF 2.5h and CCS 2.4r apply together; older XDP-era Integrated Feed
  specs (Arca XDP 1.x) are not used.
- Errata and change tables: revision rows that change the layout are listed in section 1
  (prices signed, trade conditions, Side bytes, imbalance reserved field, new mapping
  fields). The 2019 and March 2022 samples predate some of them (Side 0x00, trade
  conditions 0x00).
- Tables across page breaks: 103 (IF p.12-13), 105 (p.15-18), 110 (p.20-21), 3 (CCS
  p.14-15), 34 (p.16-18), 15 (p.23-24); every row read from the page images and
  `pdftotext -layout`; the `--check` offset chain confirms no row is missing.
- Offsets and length fields: 0-based; MsgSize counts itself (CCS p.9); PktSize includes
  the header (p.8). Table offsets are the spec's minus 2.
- Reserved fields: kept (Reserved1/2 in 100-104, 106; Reserved in 3, 34, 105; SymbolSeqNum
  of message 2 described as reserved).
- Enumerations elsewhere: product IDs in IF Appendix A; security status codes in CCS 4.5.
  Copied into descriptions.
- Field names differing: NextSourceSeqNum (32) versus SymbolSeqNum elsewhere; CCS prose
  "LastSequenceNumber" versus field LastSeqNum (p.20, p.26); "Reason Code" in the 111
  prose versus field CrossType (IF p.22).
- Examples contradicting tables: no byte examples. CCS p.9 Example 1 (an illustration of
  MsgSize) has inconsistent offsets (OrderID 12 length 4, Price 16, Volume 28); ignored.
- Two layers in one table: packet header, control, Request Server and Integrated Feed
  messages are separate sections; type codes do not collide.
- Repeated names with different codes: none. One code with two lengths: Refresh Header 35
  (16 and 8 bytes), section 10.
- Variable-length trailing field: none.
- Odd integer widths: none.

**2. Framing and encoding**

- Endianness: little-endian throughout (CCS p.9).
- Signed/unsigned: prices signed (CCS p.11, v2.4b); IDs unsigned (p.11); other Binary
  fields taken as unsigned (not stated).
- Fixed-point prices: per-symbol PriceScaleCode from message 3 or the file.
- Null and sentinel values: Halt Condition ~, SSRState ~ (CCS p.17-18); SSR Triggering
  Exchange ID 0x20 when unused; prices and Time 0 when unused; ImbalanceSide, UnpairedSide,
  RPIIndicator and TradeCond2-4 space for none.
- Strings: NUL padded per CCS p.9; FirmID space filled (observed).
- Several messages per packet: yes (NumberMsgs); a message never straddles packets.
- Timestamps: seconds + nanoseconds since the Unix epoch, UTC; most order messages carry
  only nanoseconds, seconds from message 2 per partition (CCS p.10).

**3. Sequencing and recovery**

- Scope: per channel (CCS p.10); symbol sequence per symbol across the feed (p.11).
- A/B arbitration: reuse the existing arbitration by sequence number; retransmissions may
  repacketize (CCS p.20), so arbitrate per message.
- Gap rule: section 5; heartbeat SeqNum observed to be the next expected number.
- Recovery mechanism: TCP-requested retransmission on multicast, TCP-requested refresh on
  multicast, symbol mapping refresh, and unrequested refresh after Symbol Clear.
- Snapshot alignment: per symbol by LastSeqNum / LastSymbolSeqNum; boundary reading in
  section 10.
- Snapshot too old: not applicable as such (the refresh is current); quotas bound retries
  (5,000 refreshes per day). Status 6 ("request SeqNum TTL too old") tells a client to use
  refresh instead of retransmission (CCS p.28).
- State to clear on reset: section 5 (books, order-ID map, trade-ID map, symbol
  sequences, source-time seconds); per symbol on Symbol Clear and on Security Status X.
- Recovery paths: to be exercised in replay by injected gaps, duplicates, resets,
  failover (Delivery Flag 10) and Symbol Clear. The startup sample contains a reset and
  a full Symbol Clear spin; the refresh sample a full all-symbol refresh.
- Session behaviour: no logon on multicast; Request Server TCP with heartbeat every ~60 s
  and 5 s response; reset at start of day; Module A/B single connection.

**4. Book semantics**

- MBO, not price level.
- Level-index updates: not applicable.
- Depth-limited books: not applicable.
- Queue priority on modify: price change loses, same price keeps (IF p.10); size-up at
  the same price conflicts with OB (section 10).
- Implied, overlay, iceberg: none displayed; reserve quantity hidden.
- Trades also as book deltas: no. 103 both reports the trade and reduces the order; no
  Delete follows a full execution (IF p.11). Cross Trade duplicates the per-order
  executions' volume (IF p.22).
- Trade side: 103 is the resting order's side; 110 has none.
- Trading states: section 6; crossed-book legality not stated.
- Book clears: Sequence Number Reset, failover, Symbol Clear, Security Status X (no
  Deletes), explicit Deletes at session changes.

**5. Prices, quantities and instruments**

- Price or yield: price only.
- Quantity multipliers: none; shares.
- Decimals sentinels: none; PriceScaleCode 3, 4 or 6.
- Price-dependent ticks: MPV per symbol; sub-dollar ticks not stated.
- Instrument definitions: message 3 and the daily file; SymbolIndex → securityID.
- Lifecycle: daily Order and Trade IDs; Price Scale changes nightly; test symbols type T.

**6. Behaviour the specification omits**

- Subscription options: none; multicast.
- History on subscribe: none on multicast; refresh on request.
- Snapshot is the whole book: yes, every resting order of the symbol (IF p.19).
- Order of messages within one event: not stated; an execution of an incoming order
  against several resting orders is reported as one 103 per resting order (IF p.12);
  packets holding two 103s occur (373 in the first 300,000 packets of the NYSE hour).
- Connection limits: Request Server closes after a missed heartbeat response (5 s); DOS
  lockout after 100 connects or rejects.

**7. Post-trade** — not applicable: market data only.

- FIX version: not applicable.
- Venue dictionary: not applicable.
- Credentials in Logon: not applicable (Request Server uses Source ID and registered IPs).
- Missing-field Reject: not applicable.
- Requests before session open: not applicable.
- Trade capture subscription: not applicable.
- Report lifecycle: Trade Cancel and Cross Correction are the only corrections.
- Duplicates on reconnect: not applicable.
- Fixed-income fields: not applicable.
- Daily sequence reset: Sequence Number Reset at start of day (section 5).

**8. Order entry** — not applicable: the feed cannot enter orders. Order and Trade IDs
correlate with the Pillar gateway (CCS p.11-12, 3.6.1-3.6.2), which matters to a future
NYSE order manager.

- Client ids: not applicable.
- Cancel and replace ids: not applicable (feed Replace gives a new Order ID).
- Fill races: not applicable.
- Unsolicited cancels: not applicable.
- Busts as fills: not applicable.
- Throttling: not applicable.
- Reconnect duplicates: not applicable.
- Orders placed elsewhere: not applicable.
- Short sales: not applicable (SSR state is reported in message 34).

## 10. Ambiguities

Sample captures used to settle questions (public, https://ftp.nyse.com/Real Time Data
Samples/): `NYSE XDP/NYSE_IBF/20190122.*.NYSE.IF.89_100.A.02.pcap` (two hours, one
channel, line A), `NYSE XDP/XDP_IBF_2.2.pcap` (startup with reset and spin),
`NYSE XDP/NY_IF_REFRESH_XDP_SAMPLE.pcap` (refresh channel),
`NYSE_National_XDP/Pillar-March-2022-sampledata.pcap` (several feeds mixed). All predate
IF 2.5h; they predate the v2.4/v2.5 field changes.

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Modify at the same price with a larger volume: IF p.10 says the position is kept whenever the price is unchanged. Kaspar OB turns a size-up Change into cancel and add (`OB.cpp:2430`). | Spec: priority kept. Emitting a Change loses it in OB; Kaspar cannot express a size-up that keeps priority. | NYSE in fact resets priority on size up (common elsewhere), and IF p.10 is incomplete. | NYSE. Frequency: 4 of 24,545 same-price modifies in one NYSE hour (2019). |
| PositionChange (IF p.10): "Binary", "currently defaulted to 0", values written '0'/'1'. | Ignored; priority from the price comparison. | — | Sample: 0x00 in every modify (49,390 + 177 + 116), including price changes. |
| FirmID "blank-filled" (IF p.9, p.19) against NUL padding (CCS p.9). | Spaces. | NUL. | Sample: all 596,033 + 5,793 Add Orders spaces. |
| Refresh alignment boundary: discard "lower than" LastSeqNum (CCS p.20) and apply "higher than" LastSeqNum (p.21); is the message numbered LastSeqNum in the refresh? | Yes ("state of the order book as of this sequence number", p.25): per symbol, skip messages with SymbolSeqNum <= LastSymbolSeqNum (SeqNum <= LastSeqNum), apply the rest. | The message numbered LastSeqNum must still be applied. | NYSE, or a replay comparing a refresh with the rebuilt book. |
| LastSeqNum is "the last sequence number sent on the channel for any symbol" (CCS p.25, p.26) but "will not be the same per symbol" in an all-symbol refresh (p.21). | Per symbol, from that symbol's full Refresh Header. | One value for the whole refresh. | Sample: 12 distinct values in 33,935 headers of one refresh. |
| MsgSize of the 8-byte Refresh Header (CCS p.25-26). | 8; decode() reports `malformed` with size 8 and the handler reads `RefreshHeaderShort`. | — | Not observed: every symbol in the refresh sample fit one packet. |
| Single-symbol refresh: multi-packet responses use flag 19 throughout, no 18/20 (CCS p.27). | End of a symbol = CurrentRefreshPkt == TotalRefreshPkts, not the delivery flag. | — | Spec text; not observed. |
| Queue order within a price level after a refresh: the order of Add Order Refresh messages is not stated; their SymbolSeqNum values are not monotonic within a symbol (sample). | Message order is time priority within a level. | Order by SymbolSeqNum; or unknown. | NYSE. Affects simulated queue position after recovery. |
| Refresh channel packet SeqNum: own sequence per refresh channel? | Yes, checked only to detect loss, then re-request (CCS p.21). | — | Sample: refresh packets numbered 17,810-126,865, one gap of 3. |
| Symbol Clear NextSourceSeqNum: symbol or channel sequence? | Next SymbolSeqNum of the symbol. | Channel sequence. | Sample: 7,638 of 7,638. |
| Heartbeat SeqNum value (not stated). | Next expected sequence number; usable for gap detection. | — | Sample: 1,645 of 1,645 (publisher), 14,284 of 14,284 (refresh). |
| Security Status X cancels orders without Delete messages (IF p.11). Does it apply to every X, including an X followed by an overnight or next session (IF v2.5h hours, Arca 8:59pm-8:00pm)? | Every X deletes the symbol's resting orders in the handler. | Orders for a later session survive X. | NYSE. |
| Add Order with an Order ID that is live (routed order returning, IF p.9). | Delete then add: new priority. | The returning order keeps its old priority. | Sample: 0 occurrences in one NYSE hour (line A). NYSE. |
| Replace priority (IF p.14) not stated explicitly. | New order, new priority. | — | Implied by "removed ... replaced with the new order". |
| Message Unavailable (31): IF p.6 lists it on the request channel from the server; CCS p.25 says Retransmission multicast with Delivery Flag 21. | Multicast, as CCS. | TCP. | NYSE or a recovery test in CERT. |
| Request Server TCP framing: requests are in packets (CCS p.22-23); heartbeats are packet headers (p.8); responses (11) are not said to be in packets. | Every TCP message is in a 16-byte-header packet. | Bare messages. | CERT session. |
| Request Response Status: "ASCII", values 0-9 (CCS p.28). | ASCII digits ('0' accepted). | Binary 0-9. | CERT session. |
| Imbalance for NYSE National: IF covers National (p.1, p.27) but the per-market columns are NYSE, American, Arca, Texas (p.15-18). | National like American/Arca/Texas. | — | NYSE; the National sample has 1,552 Imbalance messages of 73 bytes, field values not compared. |
| SymbolIndex across markets: same for all NYSE equity markets (CCS p.12) versus "cannot be used to cross reference a security between markets" (p.14). | Key instruments by (market, SymbolIndex). | Global key. | NYSE symbol mapping files of two markets. |
| TradeID width: "8 byte integers" (CCS p.11, 3.6) versus 4 bytes (3.6.1 and every table). | 4 bytes; unique per partition, so key Trade Cancel by (SymbolIndex, TradeID). | — | Tables; sample decodes. |
| Source Time Reference ID: "per TXN, which is equivalent to the Integrated Feed channel number" (CCS p.10) and linked to System ID (p.13). | Join by System ID of the symbol. | By channel. | Startup sample: ID 2, System ID 2 for all 7,638 symbols (one partition only, so not a full check). |
| CCS version: cover 2.4r, revision row "2.6r" for July 21, 2026 (CCS p.3). | 2.4r. | — | Cover and file name. |
| TradeCond1 has an empty third bullet (IF p.13). | Values @ and C only. | A value lost in editing. | NYSE. |
| Order Execution price different from the order price (IF p.12; 242 of 8,576 in one hour). `l3_mbo_trd_v2_t` has no price. | Fill priced at the book price by OB; log the difference. | Add a price to the trade record (a Kaspar change). | User. |
| Auction executions: per-order 103/110 with PrintableFlag 0 plus a Cross Trade with the bulk volume (IF p.12, p.22). Kaspar trade records have no printable flag. | Emit trade records for every 103 (they reduce orders); no record for 111. | Drop non-printable executions from volume statistics (needs a flag in the record). | User. |
| Event boundaries for `endOfEvent`: no event flag; several messages of one match may share a packet and SourceTimeNS. | Mark endOfEvent at a change of SourceTimeNS or at the end of the packet. | Every message its own event. | Capture analysis. |
| Overnight session (IF v2.5h, Arca 8:59pm) against "feed start 2:03am" and "Order IDs valid for the current day" (CCS p.11, p.31). When the trading day, Order ID scope and sequence reset start for overnight orders is not stated. | Day boundary at the Sequence Number Reset. | — | NYSE. |
| National sample: types 140 and 220 (not in IF) and 16 sequence gaps keyed by destination port. | Other feeds captured in the same file; not counted as Integrated Feed errors. | — | File description from NYSE. |
