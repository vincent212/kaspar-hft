# Non-certified venue work

**Not for use against a venue.** The contents were produced from public
specifications and checked offline: against the specifications' own examples,
against public sample captures where the venue publishes them, and against the
venue's machine-readable message layouts where it publishes them. None of it has
connected to a venue, and a venue's certification environment has not been used.

| Directory | Contents |
|---|---|
| [`market_data/<venue>/`](market_data/) | Feed description (`feed.md`), field tables (`*.tbl`) that generate the decoders, decode tests, and the review list of open questions (`review.md`). Handlers are added here as they are written. |

Order managers go in `order_entry/<venue>/` and post-trade clients (drop copy,
trade capture) in `post_trade/<venue>/`, when there are any.

What "checked" means for each venue is stated in its README and in section 1 of its
`feed.md`. The open questions about each specification are in section 10 of its
`feed.md` and summarised in its `review.md`.

A venue moves to `../certified/` when it meets the criteria in
[`../certified/README.md`](../certified/README.md).
