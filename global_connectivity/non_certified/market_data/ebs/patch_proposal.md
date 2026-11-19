# Changes Kaspar needs to receive EBS Market data

Not applied. `mdp3/`, `genschema/`, `genconfig/`, `chutil/` and `kaspr/` are
unchanged by this work. Items follow the gaps in `feed.md` section 7; decisions refer
to `review.md`. Item 9 concerns the existing CME futures path as well.

## 1. Venue code

`chutil/gencode/enum/def/exch_code.enum`: append `EBS` at the end and regenerate with
`chutil/gencode/enum/genenum.py` (`kaspar_mapping.md`, Venue codes). `universe_ebs.csv`
uses it in the exchange column.

## 2. Callbacks for the long-quantity and FX templates (`mdp3/include/mdp3/mbo_if.hpp`)

Add to `feed_handler_if`, with empty default bodies so every existing implementor
(`handler_if`, `unit_test::NullFeedHandler`, the recorders) still compiles:

```cpp
// 64. Carries MDUpdateAction, which the template-46 callback drops (gap G1),
// and the quantity as uint64 (gap G2).
virtual void MDIncrementalRefreshBookLongQty(
    uint64_t recv_time, uint32_t msgSeqNum, uint64_t transactTime, uint64_t sendingTime,
    int32_t securityID, int64_t px_mantissa, int8_t px_exponent,
    char entryType,        // '0' '1' 'w' 'x'
    uint8_t updateAction,  // 0 1 2 5
    uint8_t priceLevel, uint64_t qty, int32_t numOrders,
    bool endOfEvent, bool recovery) noexcept {}

// 65
virtual void MDIncrementalRefreshTradeSummaryLongQty(
    uint64_t recv_time, uint32_t msgSeqNum, uint64_t transactTime, uint64_t sendingTime,
    int32_t securityID, int64_t px_mantissa, int8_t px_exponent, uint8_t aggressorSide,
    uint64_t qty, uint32_t tradeEntryID, uint8_t updateAction,
    bool lastTrade, bool endOfEvent) noexcept {}

// 66
virtual void MDIncrementalRefreshVolumeLongQty(
    uint32_t msgSeqNum, uint64_t transactTime, uint64_t sendingTime,
    int32_t securityID, uint64_t cumulativeQty) noexcept {}

// 63: one struct rather than ~50 scalar arguments.
struct fx_definition_t {
  char updateAction; uint8_t tradingStatus; int16_t applID; uint8_t marketSegmentID;
  char securityExchange[5], securityGroup[7], asset[7], symbol[21], securityType[7],
       fxCurrencySymbol[8], fullName[36];
  int32_t securityID; char matchAlgorithm;
  uint32_t minTradeVol, maxTradeVol, minQuoteLife, altMinQuoteLife;
  double minPriceIncrement, altMinPriceIncrement /* NaN if null */, displayFactor;
  uint8_t pricePrecision; int8_t marketDepth /* GBX entry of 1141 */;
  uint64_t lotIncrement /* LotType 5 */, sizePriorityQty /* LotType 6, 0 if absent */;
  uint64_t activation, lastTrade;
};
virtual void MDInstrumentDefinitionFX(uint32_t msgSeqNum, uint64_t sendingTime,
                                      const fx_definition_t &d) noexcept {}

// 69
virtual void SnapshotFullRefreshLongQty(
    uint32_t lastMsgSeqNumProcessed, uint32_t totNumReports, int32_t securityID,
    uint32_t rptSeq, uint64_t transactTime, uint8_t tradingStatus,
    const std::vector<std::tuple<char, int64_t, uint64_t, int32_t, uint8_t>> &entries
    /* type, px mantissa, qty, orders, level */) noexcept {}
```

## 3. Decoders (`mdp3/include/mdp3/msg_decoder.hpp`)

`decode_MDInstrumentDefinitionFX63`, `decode_MDIncrementalRefreshBookLongQty64`,
`decode_MDIncrementalRefreshTradeSummaryLongQty65`,
`decode_MDIncrementalRefreshVolumeLongQty66`, `decode_SnapshotFullRefreshLongQty69`,
written like the existing ones. Two rules from the tests:

- SBE groups decode in order: every entry of a group must be consumed before the
  next group is read (NoEvents before NoMDFeedTypes before NoInstAttrib ...).
- Read the four version-13 fields of 63 only when `altMinPriceIncrementInActingVersion()`
  (test `VersionTwelveDecoderSeesNoAltTickFields`).

## 4. Dispatch

- `DataDecoder.hpp` `decode_one`: cases for 63, 64, 65, 66, each wrapping with
  `wrapForDecode(databuf, 10, BlockLength, Version, MsgSize)` as the others do. The
  existing cases (54 included) log a decode exception and continue; for 63 and 64 a
  failed decode leaves an instrument or a book wrong, so they should return false and
  start recovery.
- `act/InstrumentRecoveryRecorder.hpp`: case 63.
- `act/RecoveryProcessor.hpp`: case 69 (decision 3).

## 5. An EBS feed handler instead of extending `handler_if`

`handler_if`'s MBP code is dead (`filter_mbp_ = true`, `#ifdef mbp`, a nonexistent
`mbp_order_books`), and its state is MBO-specific (the orderID map, `TreasOnly`).
Proposed: `mdp3/include/mdp3/ebs_if.hpp`, `struct ebs_handler : feed_handler_if`, per
channel, holding per instrument:

- `ebs::MbpLevelBook` (prototype in `include/ebs/MbpLevelBook.hpp` here),
  `sim::LevelWindow` (depth = MarketDepth), `sim::Mbp2Mbo` (sec_id, venue
  `en::x::EBS`, tick per decision 7, chunk per decision 6), emitting into the asset's
  OB as `frame::mda::msg::Data`.
- On 64: apply 0/1 entries to the `MbpLevelBook`, ignore w/x; on end of event
  `publish`, `flush`, `end_event`. An entry that does not fit (`apply` returns false)
  is a book error: `l3_gap_v2_t` to the book and request recovery.
- On 65: `l3_mbp_trd_t` print; executions per decision 1.
- On 66: keep the cumulative total per instrument (decision 2).
- On 63: map Asset (decision 4) to the Kaspar asset; set `units` 1, record tick and
  chunk; create the per-instrument objects.
- On 30: group (48 null) to the group's books, instrument (1151 null) to that book.
- On 4 and DR: clear every `MbpLevelBook`, `LevelWindow`, `Mbp2Mbo` and volume total
  on the channel; `l3_chr_v2_t` to each book.

## 6. `sim::Mbp2Mbo` (decision 1 option a)

A call that executes a given quantity from the front of a level without a trade size:
`execute(bool bid, double px, double qty, uint64_t tx)`, used by the EBS handler with
the decrease at the trade price inside the event. `trade()` today executes only when
`qty` is positive (test `ZeroSizeTradeExecutesNothing`).

## 7. `kaspr/src/kaspr.cpp`

- Create an OB for every asset whose `exchange_md` is `en::x::EBS` (today only
  `CMEMDFUT` with mnemonics ES / ES, NQ, ZN).
- Start EBS channels from `cme.ini` with `ebs_handler` instead of `handler_if`, venue
  `en::x::EBS`; `cme_disable_mbo true`.

## 8. `genconfig/genconfig_mdp3_3.py`

- Fetch `SBEFix/<env>/Configuration/Ultra{NY,London,Off}/EBS*Config.xml` as it fetches
  `config.xml` and the US Actives files.
- Map `<chan>SA` to `_dr` for MBP-recovery channels (it maps only ids containing
  `AMBO`). `config/ebs_mdp3_prod.info` here is what the result would look like.

## 9. Security Status routing in `handler_if` (CME futures too)

`handler_if::SecurityStatus` sends every status to every book on the channel. SST
documents instrument-level messages (48 set, 1151 null) for futures as well: Pre-Open
(Reserved) from velocity logic, Pause, Close (Forbidden). On such a message today,
every OB on the channel takes the instrument's state. The comment in the code says
INT32_MAX (group scope) was observed on every record of four channel-310 sessions, so
this may not have occurred on the channels Kaspar records; it was not checked on
data here. Fix: when `securityID` is not the null value, send to that instrument's
book only.

## 10. Tests to change with the patch

`tests/ebs_mdp3_test.cpp`: the four `Gap_...` tests assert today's behaviour and will
fail once items 2-4 land; replace them with assertions that the new callbacks fire
with the encoded values (including a 3,000,000,000 level and Overlay).
