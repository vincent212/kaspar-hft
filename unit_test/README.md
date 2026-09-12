# unit_test — Google Test suite

295 tests over the pieces that decide what the simulator does: the shadow light,
the coordination objects it shares, reference data, the timer, the simulated
order manager, and position tracking.

## Running

```bash
export KSPRPROJ=~/kaspar-hft
eval "$(mk_kaspr/detect_paths.sh)"      # must produce GTEST_PATH
make test                               # builds libs, builds tests, runs them
```

Or directly:

```bash
cd unit_test/src && make && ./run_tests
./run_tests --gtest_filter='Light22IntegrationTest.*'
./run_tests --gtest_list_tests
```

`make test` is deliberately **not** part of `make install`: gtest is an extra
dependency, and a fresh checkout should build the system without it. If
`detect_paths.sh` does not find gtest, `detect_paths.sh --check` prints install
hints (`apt: libgtest-dev | dnf: gtest-devel | brew: googletest`), or build it
into a home prefix:

```bash
git clone --depth 1 -b v1.14.0 https://github.com/google/googletest
cmake -S googletest -B build -DCMAKE_INSTALL_PREFIX=$HOME/local
cmake --build build -j8 && cmake --install build
```

## What is covered

| File | Subject |
|---|---|
| `test_cache_array.cpp` | `chutil::cache_array` |
| `test_qcoord.cpp` / `test_pcoord.cpp` | working-order and position coordination |
| `test_refdata.cpp` | universe loading, asset lookup |
| `test_price_units.cpp` | tick units vs the price ladder they address |
| `test_timer.cpp` | `frame::mtim::Timer` |
| `test_som.cpp` | simulated order manager |
| `test_position.cpp` | position arithmetic |
| `test_position_manager.cpp` | `AddToPos` → `PCoord` |
| `test_light22.cpp` | light22 messages, enums, placement/cancel conditions |
| `test_light22_order.cpp` | order lifecycle |
| `test_light22_integration.cpp` | real `light22` against mock SOM/OB/Timer |
| `test_som_cancel_latency.cpp` | the ts0 SOM stamps on orders and cancels |
| `test_ob_book.cpp` | OB: book reconstruction and the no-cross invariant |
| `test_ob_delay_queue.cpp` | OB's delay queue: the wire-latency model |
| `test_slippage_probe.cpp` | the probe's 30-minute fire cadence |

`test_light22_integration.cpp` is the one that matters most: it instantiates the
actual `light22<BUY>` / `light22<SEL>` and drives it with `EndOfBurst` messages,
so it tests the code the simulator runs rather than a restatement of it.

Not ported from upstream: `test_aggregator`, `test_market_maker`,
`test_cvol_formula`, and the `SpreadPulseTest` suite — Aggregator, MarketMaker,
CvolActor and the spread path do not exist in this repo.

## Fixture data

`unit_test/config/universe.csv` — a six-instrument universe, self-contained on
purpose. Five rows use `units 1`, which hides every price conversion; **ESZ5 is
shaped like the real thing** (native `minPriceIncrement` 25, `maxpx` 25592 =
639800/25) and is what any test touching the price ladder should use. `sim/` has no universe CSV (it seeds `RefData` empty and registers
instruments from the universe JSON), so the tests must not depend on it.

## Adding a test

Add the `.cpp` to `SRC` in `unit_test/src/Makefile`. The Makefile uses the
standard `mk_kaspr` app template; the only local additions are the gtest include
and link flags.

## Mocks

`unit_test/include/unit_test/`:

- `MockActor.hpp` — `MockSOM`, `MockOB`, `MockTimer`, `MockSuper`, `MockDB`;
  each records the messages it receives. `has_message_of_type<T>()` is the
  usual assertion.
- `MockQCoord.hpp` / `MockPCoord.hpp` — record calls, let a test set position
  and level size directly.
- `TestHelper.hpp` — `invoke_handler()` dispatches one message to an actor
  synchronously, without starting its thread.
- `FakeMarketData.hpp` — builders for `EndOfBurst` payloads.

Actors are driven synchronously through `TestHelper::invoke_handler`. Nothing
starts a Manager or a thread, so the tests are deterministic and the whole suite
runs in about 6 ms.
