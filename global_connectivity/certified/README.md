# Certified venue connectivity

Empty. No venue developed under `global_connectivity/` is certified yet.

A venue's market-data handler, order manager or post-trade client moves here from
`../non_certified/` when all of the following hold, and the owner has signed off:

1. **Specification review.** Every item under "Needs your decision" in its
   `review.md` is decided, and the handler implements the decisions.
2. **Real data.** Market data: a full trading day of the venue's data, recorded or
   live, has run through the handler into Kaspar's books with no decode errors, no
   sequence gaps left unrecovered, and the book invariants holding (no crossed book
   in continuous trading, every delete and execution matching a known order); and
   the books agree with a reference (the venue's snapshot service, vendor data, or
   the venue's own decoder) at sampled times. Order entry and post-trade: the
   venue's certification or conformance test has been passed in its test
   environment.
3. **Recovery.** Gap recovery, snapshot alignment, reconnect and session reset have
   each been exercised against the venue's test environment or recorded data, not
   only against a simulator.
4. **Operation.** The handler has run unattended through at least one full trading
   session in a development environment on live venue data.

Its README then records the dates, the environment, the data and the results of
each step. The certified CME and TT components (`mdp3/`, `ilink/`, `ttom/`) predate
this directory and stay where they are.
