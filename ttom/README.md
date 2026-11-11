# ttom: Trading Technologies FIX order routing

`ttom` routes orders to [Trading Technologies](https://www.tradingtechnologies.com/) (TT) over FIX 4.4. It is the TT counterpart of `ilink/`, which routes to CME directly over iLink 3.

| part | file | what it does |
|---|---|---|
| `TTFixClient` | `include/ttom/TTFixClient.hpp`, `src/TTFixClient.cpp` | QuickFIX application: logon, NewOrderSingle, OrderCancelRequest, OrderCancelReplaceRequest, execution reports, cancel rejects, TT News |
| `TTFIXOM` | `include/ttom/act/TTFIXOM.hpp` | actor: takes `frame::som::msg::Order` and `Cancel` from the SOM, sends them through `TTFixClient`, and answers with `Ack`, `Fill`, `CancAck`, `Reject` and `CancReject` |
| factory | `interface/ttom/if/TTFIXOM.hpp`, `src/ttfixom.cpp` | `create_TTOM(som, cfg, en::x::TT)`, and `create_TTFIXOM(cfg)` without a SOM |
| test | `test/tt_logon.cpp`, `run_tt_test.sh` | standalone session check: logon, one order and its cancel, logout |
| config | `config/tt_example.cfg`, `config/stunnel.conf` | QuickFIX session template and TT's stunnel template |

On `Start`, `TTFIXOM` starts the FIX session and registers with the SOM as the order manager for venue `en::x::TT`, so the SOM forwards orders for that venue to it.

## Build

`ttom` needs the QuickFIX FIX engine, so it is built only on request.

1. Install QuickFIX from the official repository, [quickfix/quickfix](https://github.com/quickfix/quickfix). Build it with OpenSSL if TLS is to be handled by QuickFIX rather than by stunnel (below). It installs under `/usr/local` by default; pass `QUICKFIX_PREFIX=<dir>` to the builds below if it is elsewhere. If OpenSSL is outside the default paths (on macOS, Homebrew's `/opt/homebrew/opt/openssl@3`), also pass `OPENSSL_PREFIX=<dir>`.

   For a working TT client on QuickFIX to compare against, see [vincent212/FIX-Order-Routing-Client](https://github.com/vincent212/FIX-Order-Routing-Client).

2. Build the libraries with `ttom`, then kaspr and the test program:

   ```bash
   KSPRPROJ=~/kaspar-hft make USE_TTOM=y
   cd kaspr/src  && KSPRPROJ=~/kaspar-hft make USE_TTOM=y
   cd ttom/test  && KSPRPROJ=~/kaspar-hft make opt
   ```

   Add `TTOM_SSL=y` to the `ttom` and test builds to use QuickFIX's own TLS (`SSLSocketInitiator`) instead of stunnel.

Without `USE_TTOM=y`, `ttom` is not built and kaspr has no QuickFIX or OpenSSL dependency.

## Configure

TT's FIX gateway requires TLS. Get these from TT:

- **Session details** for your FIX session: password, account, SenderCompID, TargetCompID, FIX user (tag 116, if your account has several users), and the gateway host and port.
- **`TT-FIX44.xml`**, TT's FIX 4.4 data dictionary, from the FIX schemas section of TT's FIX documentation (UAT and production versions): https://library.tradingtechnologies.com/tt-fix/general/System_Overview.html#fix-schemas. Save it in `ttom/config/`.
- **`TTFIX.crt`**, the certificate chain for TT's gateway, in TT's stunnel package: https://library.tradingtechnologies.com/tt-fix/general/stunnel.zip. Save it in `ttom/config/`.

Then:

1. Copy `config/tt_example.cfg` to, for example, `config/tt_uat.cfg` and fill in every `<...>` value. Real configs are ignored by git (`config/.gitignore`), as are `TT-FIX44.xml`, `TTFIX.crt` and QuickFIX's `log/` and `filestore/` directories.
2. Choose how TLS is done:
   - **stunnel** (TT's recommendation): fill in `config/stunnel.conf` with a local port and TT's host and port, run `stunnel config/stunnel.conf`, and point the session at `127.0.0.1` and the local port.
   - **QuickFIX TLS** (`TTOM_SSL=y`): point the session at TT's host and port and set `ClientCertificateFile=TTFIX.crt`.

## Test the session

```bash
ttom/run_tt_test.sh tt_uat.cfg                                    # logon only
ttom/run_tt_test.sh tt_uat.cfg <cfi_code> <security_id> <symbol> <price> <qty> <B|S>
```

The first form logs on, waits for TT's News message (TT sends it when the session can take orders) and logs out. The second also sends one limit DAY order, cancels it once it is acknowledged, and prints every execution report.

## Use in kaspr

In a `USE_TTOM=y` build, kaspr creates the TT order manager when `kaspr.ini` has:

```
kaspr {
    tt {
        enabled true
        cfg /home/me/kaspar-hft/ttom/config/tt_uat.cfg   ; absolute, or relative to kaspr's config directory
    }
}
```

QuickFIX resolves the paths inside the session file (`DataDictionary`, `FileLogPath`, `FileStorePath`, `ClientCertificateFile`) from kaspr's working directory, so make them absolute in the file kaspr uses.

The order manager registers with the SOM for venue `TT`. The SOM forwards an order to it when the SOM is not in simulation mode and the order's venue is `TT`. kaspr currently creates its SOM in simulation mode, so in kaspr the TT order manager logs on but receives no orders; `tt_logon` drives the session directly.
