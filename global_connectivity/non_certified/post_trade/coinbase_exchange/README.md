# Coinbase Exchange drop copy (FIX 5.0 SP2)

**Not certified.** `CbxDropClient` has never connected to Coinbase Exchange,
the sandbox included. It has been tested against a fake session and an
in-process simulator. The feed description is `feed.md`.

## Contents

| Path | What |
|---|---|
| `feed.md` | Feed description: logon, reports, the mapping to `TradeCapture`, pitfalls, ambiguities |
| `include/coinbase_exchange_pt/act/CbxDropClient.hpp`, `src/CbxDropClient.cpp` | The client actor and `to_trade_capture` |
| `include/coinbase_exchange_pt/SimDropCopy.hpp`, `src/SimDropCopy.cpp` | In-process drop copy for `--sim` and tests |
| `config/cbx_drop_example.ini` | Configuration |
| `tests/test_cbx_drop.cpp` | 13 tests |

The session (`CbxSession`, `QfSession`, `FixMsg`) is the order-entry
directory's, with `drop_copy` set; the output is `post_trade::msg::TradeCapture`
through `post_trade::TradeLedger` (`../common`).

## Build and test

```bash
export KSPRPROJ=~/kaspar-hft
make test [QUICKFIX_PATH=/path/to/quickfix]
```

## Checked (2026-11-18, macOS, clang)

- 13 of 13 tests pass (with and without QuickFIX): an ExecutionReport Trade
  maps to `TradeCapture` field by field (trade id, report id, side, quantity in
  base increments, exact price, UTC date and time, poss-dup); order events are
  not trades; a report without TradeID; Trade Cancel/Correct (undocumented) map
  to Bust/Amend; malformed reports are refused; the actor logs on as a drop-copy
  session (9406=Y, no 8013); trades reach the subscriber once (the same ExecID
  again and a PossDupFlag resend are dropped by the ledger); a bust removes the
  trade; a reconnect does not duplicate; a rejected logon stops the client; the
  simulator's 9 trades and 3 resends give 9 TradeCaptures; the configuration
  file is read.
- The only wire-format ExecutionReport in Coinbase's documentation (the
  changelog, 2025-SEP-3) maps once its BodyLength and CheckSum, which do not
  match its content, are removed.

## Not checked

- Anything against Coinbase (no sandbox keys; step 3 below).
- Whether the drop copy carries TradeID (1003) on every fill: the changelog adds
  it to FIX market data (2025-10-09) and the ExecutionReport tables list it.
- Fills missed while disconnected are not recovered by this client (feed.md
  section 10).

## Credentials

As the order manager (`../../order_entry/coinbase_exchange/README.md`,
"Credentials"); the drop copy uses `dc_key`, `dc_secret`, `dc_passphrase` from
the key file when present.

## Step 3 of the testing ladder (sandbox): what will run

With the key file present, against `fix-dc.sandbox.exchange.coinbase.com:6122`
only: log on with 9406=Y; while the order manager places, cancels and fills the
orders of its own step 3, check that every Trade report becomes one
`TradeCapture`, that its quantity, price and TradeID match REST
`GET /fills?order_id=` on `api-public.sandbox.exchange.coinbase.com`, that order
events are counted and not forwarded, and that a reconnect produces no
duplicate.
