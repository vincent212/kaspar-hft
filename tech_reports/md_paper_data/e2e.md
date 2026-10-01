<!-- Generated 2026-10-01 14:25 EDT from /home/vincent/perf/mdperf/paper -->
# End-to-end t1-t0 (us), pooled over passes, first 120s of each run dropped

## Legend

**What is measured.** t1 - t0 per message, in microseconds:

- t0: user-space timestamp on the socket-reader thread, right after `recvfrom`
  returns the packet.
- t1: timestamp in TachBook (the MBO book) just before it publishes the updated
  book.

Time before t0 (NIC, socket buffer) is not included. Stream: front-month
contract (Z6), `book` = book updates, `trade` = trades.

**Columns.**

| column | meaning |
|---|---|
| config | label below |
| runs | number of 8-10 minute runs pooled |
| msgs | messages measured, after dropping the first 120 s of every run (startup recovery) |
| p1..p999 | percentiles of t1 - t0 over those messages |
| max | slowest single message |

Rows in the per-stream tables are sorted by p90, fastest first.

**Config labels.** Every config is `base` plus the listed changes. Exact files:
`configs/<label>/`. Full description: `../md_median_vs_tail_draft.md`,
section "Configuration labels".

| label | what changes vs base | thread hops between t0 and t1 |
|---|---|---|
| `base` | none (production path): socket reader -> MsgBuf (sleeps between packets) -> decode inline -> book on its own thread (sleeps) | 2, both into sleeping threads |
| `fastsend` | book update runs inline on the decode thread (`book_fast_send`) | 1 |
| `mbspin` | MsgBuf busy-polls instead of sleeping (`cme_msgbuf_mailbox lockfree_spin`) | 2, first without wakeup |
| `fastsend_mbspin` | both of the above | 1, without wakeup |
| `rfs` | socket reader runs MsgBuf, decode and book inline (`cme_reader_fast_send` + `book_fast_send`) | 0 |
| `p4s` | parallel decode: 4 busy-polling workers + one resequencing handler per channel (`cme_decode_workers 4`, `cme_decode_spin`) | 4 |
| `fsmb_pin` | `fastsend_mbspin`, busy threads pinned to NUMA node 2 CPUs (not isolated), everything else kept off node 2 | 1 |
| `rfs_pin` | `rfs`, each socket reader pinned alone on a physical core | 0 |
| `<label>_A` | same as `<label>`, feed B switched off (`cme_feed_b false`); one socket reader per channel | same |

All runs: Onload kernel bypass, live CME channels 310 (ES), 318 (NQ) and 344
(ZN), 2026-10-01 08:56-13:00 ET.

**Sections.**

- **Per-pass:** p50 / p99 / p999 of each run block, as a consistency check.
  - `p1`, `p2` = matrix passes 1 and 2.
  - `x1`, `x2` = experiment rounds 1 and 2.
- **Latency by ingress qlen:** latency grouped by how many packets were already
  queued at MsgBuf when this one arrived.
  - Shows queueing in bursts.
  - For `rfs*` there is no MsgBuf queue (the reader calls it directly), so qlen
    is always 0.


## ESZ6_book
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| fastsend_mbspin_A | 1 | 321,930 | 1.5 | 1.8 | 2.3 | 3.9 | 11.1 | 23.2 | 153 |
| rfs_pin_A | 2 | 249,085 | 0.6 | 0.8 | 1.2 | 4.0 | 16.6 | 42.8 | 256 |
| rfs_pin | 2 | 308,538 | 0.6 | 0.8 | 1.3 | 4.4 | 31.9 | 253.5 | 1848 |
| rfs | 1 | 97,323 | 0.6 | 0.9 | 1.6 | 4.5 | 13.8 | 28.6 | 145 |
| fastsend_mbspin | 4 | 912,946 | 1.4 | 1.8 | 2.5 | 5.3 | 14.4 | 29.9 | 1562 |
| fsmb_pin | 2 | 342,934 | 0.9 | 1.1 | 1.7 | 5.4 | 51.0 | 739.0 | 12918 |
| fastsend_A | 1 | 288,255 | 2.3 | 2.9 | 4.4 | 6.6 | 14.4 | 29.6 | 155 |
| mbspin_A | 1 | 232,647 | 3.0 | 3.4 | 4.0 | 6.7 | 13.5 | 26.3 | 413 |
| mbspin | 2 | 798,503 | 2.7 | 3.8 | 5.3 | 8.1 | 16.6 | 34.6 | 1702 |
| fastsend | 2 | 467,341 | 2.2 | 2.9 | 4.9 | 8.1 | 19.6 | 80.1 | 3087 |
| base_A | 1 | 260,138 | 4.0 | 4.6 | 6.6 | 9.6 | 16.9 | 37.1 | 935 |
| base | 4 | 671,850 | 3.9 | 4.7 | 6.6 | 11.0 | 23.1 | 65.6 | 633 |
| p4s_A | 1 | 316,219 | 6.6 | 8.0 | 10.3 | 14.9 | 25.6 | 49.8 | 503 |
| p4s | 2 | 394,079 | 7.8 | 9.6 | 12.3 | 18.1 | 35.8 | 85.3 | 4019 |

## NQZ6_book
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| rfs_pin_A | 2 | 358,522 | 0.6 | 0.7 | 1.1 | 2.6 | 6.2 | 15.7 | 99 |
| rfs_pin | 2 | 501,247 | 0.6 | 0.8 | 1.1 | 3.0 | 8.6 | 84.0 | 1700 |
| rfs | 1 | 169,382 | 0.5 | 0.8 | 1.3 | 3.1 | 6.6 | 12.9 | 60 |
| fastsend_mbspin_A | 1 | 507,875 | 1.4 | 1.7 | 2.1 | 3.1 | 5.8 | 9.1 | 394 |
| fsmb_pin | 2 | 501,254 | 0.8 | 1.0 | 1.4 | 3.5 | 29.6 | 312.0 | 3469 |
| fastsend_mbspin | 4 | 1,482,940 | 0.9 | 1.6 | 2.2 | 3.9 | 7.2 | 15.0 | 3440 |
| fastsend_A | 1 | 431,082 | 2.1 | 2.3 | 2.8 | 4.7 | 7.5 | 12.6 | 389 |
| mbspin_A | 1 | 328,356 | 3.1 | 3.5 | 4.1 | 5.8 | 8.4 | 12.9 | 410 |
| fastsend | 2 | 698,025 | 2.0 | 2.7 | 4.7 | 6.8 | 10.3 | 20.8 | 2081 |
| mbspin | 2 | 1,392,197 | 3.2 | 3.6 | 5.3 | 7.0 | 10.0 | 17.2 | 1987 |
| base_A | 1 | 446,229 | 3.8 | 4.1 | 5.0 | 7.5 | 10.4 | 17.6 | 1651 |
| base | 4 | 1,001,584 | 3.6 | 4.3 | 6.3 | 8.9 | 12.6 | 21.9 | 10087 |
| p4s_A | 1 | 488,367 | 7.5 | 8.7 | 11.2 | 14.9 | 20.8 | 31.3 | 2615 |
| p4s | 2 | 562,774 | 6.5 | 8.2 | 11.0 | 15.6 | 22.6 | 346.8 | 13053 |

## ZNZ6_book
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| rfs_pin_A | 2 | 139,630 | 0.6 | 0.8 | 1.2 | 3.3 | 36.8 | 100.5 | 157 |
| fastsend_mbspin_A | 1 | 112,671 | 1.7 | 2.0 | 2.5 | 4.0 | 46.7 | 109.3 | 147 |
| rfs | 1 | 43,253 | 0.6 | 0.9 | 1.4 | 4.1 | 53.9 | 162.5 | 249 |
| rfs_pin | 2 | 163,661 | 0.7 | 0.9 | 1.3 | 4.4 | 104.6 | 361.5 | 1463 |
| fastsend_mbspin | 4 | 403,941 | 0.9 | 1.5 | 2.4 | 5.3 | 65.8 | 290.2 | 2180 |
| fastsend_A | 1 | 96,329 | 2.4 | 2.9 | 4.7 | 6.8 | 44.6 | 196.4 | 247 |
| fastsend | 2 | 233,671 | 1.9 | 2.7 | 4.7 | 7.8 | 54.0 | 179.3 | 1330 |
| mbspin_A | 1 | 87,992 | 3.4 | 3.8 | 5.5 | 8.0 | 46.2 | 85.4 | 124 |
| mbspin | 2 | 261,896 | 3.2 | 3.7 | 4.9 | 8.8 | 44.0 | 153.6 | 2788 |
| base_A | 1 | 79,253 | 3.9 | 4.4 | 6.6 | 10.9 | 66.0 | 266.3 | 667 |
| base | 4 | 370,252 | 3.9 | 4.6 | 7.3 | 12.0 | 63.0 | 138.0 | 2730 |
| fsmb_pin | 2 | 191,525 | 0.9 | 1.2 | 1.9 | 13.9 | 331.8 | 1282.6 | 2756 |
| p4s_A | 1 | 97,659 | 8.6 | 10.4 | 13.5 | 19.4 | 58.1 | 163.6 | 430 |
| p4s | 2 | 191,226 | 7.8 | 10.0 | 13.7 | 20.8 | 90.9 | 264.8 | 7190 |

## ESZ6_trade
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| fastsend_mbspin_A | 1 | 27,226 | 1.5 | 2.0 | 3.8 | 8.7 | 20.5 | 36.1 | 56 |
| fastsend_mbspin | 4 | 101,453 | 1.5 | 2.0 | 4.2 | 9.8 | 21.4 | 43.1 | 1106 |
| rfs | 1 | 11,361 | 0.7 | 1.2 | 3.8 | 10.6 | 29.4 | 72.7 | 85 |
| rfs_pin_A | 2 | 33,589 | 0.5 | 0.8 | 3.4 | 11.2 | 48.1 | 86.1 | 128 |
| rfs_pin | 2 | 37,278 | 0.6 | 0.9 | 3.5 | 11.3 | 66.6 | 323.9 | 740 |
| fastsend_A | 1 | 24,867 | 2.6 | 3.5 | 6.1 | 12.0 | 28.2 | 85.5 | 103 |
| mbspin_A | 1 | 25,851 | 3.2 | 3.8 | 6.0 | 12.0 | 28.0 | 55.5 | 71 |
| mbspin | 2 | 71,692 | 3.4 | 4.5 | 7.1 | 13.5 | 30.0 | 101.6 | 139 |
| fsmb_pin | 2 | 36,214 | 0.8 | 1.2 | 3.9 | 13.7 | 168.5 | 11246.8 | 12862 |
| fastsend | 2 | 53,924 | 2.6 | 3.8 | 6.9 | 14.4 | 72.8 | 261.0 | 2982 |
| base_A | 1 | 23,489 | 4.3 | 5.6 | 8.6 | 14.7 | 29.3 | 47.2 | 247 |
| base | 4 | 71,733 | 4.5 | 5.6 | 9.3 | 18.0 | 89.3 | 432.5 | 483 |
| p4s_A | 1 | 28,923 | 7.8 | 9.8 | 14.0 | 22.1 | 37.5 | 62.1 | 71 |
| p4s | 2 | 37,003 | 9.5 | 11.7 | 16.3 | 27.2 | 54.2 | 125.2 | 738 |

## NQZ6_trade
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| fastsend_mbspin_A | 1 | 11,070 | 1.5 | 1.7 | 2.4 | 4.5 | 12.2 | 34.0 | 40 |
| rfs_pin_A | 2 | 10,141 | 0.5 | 0.7 | 1.5 | 4.7 | 14.4 | 28.6 | 50 |
| rfs_pin | 2 | 12,907 | 0.6 | 0.8 | 1.6 | 5.6 | 31.2 | 90.6 | 877 |
| rfs | 1 | 3,234 | 0.8 | 1.1 | 1.9 | 5.6 | 26.6 | 37.7 | 127 |
| fastsend_mbspin | 4 | 40,615 | 0.9 | 1.6 | 2.5 | 5.7 | 22.4 | 50.4 | 1285 |
| fsmb_pin | 2 | 11,519 | 0.8 | 1.0 | 1.9 | 6.0 | 100.3 | 406.4 | 1842 |
| fastsend_A | 1 | 9,102 | 2.2 | 2.6 | 3.6 | 6.7 | 20.0 | 31.3 | 37 |
| mbspin_A | 1 | 7,261 | 3.4 | 3.9 | 5.1 | 8.3 | 17.9 | 40.7 | 45 |
| fastsend | 2 | 19,864 | 2.5 | 3.3 | 5.3 | 8.7 | 22.1 | 50.0 | 885 |
| mbspin | 2 | 43,359 | 3.3 | 3.9 | 5.8 | 8.8 | 23.7 | 39.3 | 205 |
| base_A | 1 | 10,672 | 3.9 | 4.5 | 6.3 | 9.8 | 15.9 | 29.2 | 1003 |
| base | 4 | 25,818 | 4.2 | 4.9 | 7.5 | 12.1 | 41.3 | 150.4 | 163 |
| p4s_A | 1 | 11,257 | 8.4 | 10.2 | 13.4 | 20.0 | 36.2 | 58.2 | 196 |
| p4s | 2 | 14,628 | 7.6 | 9.5 | 13.3 | 20.9 | 37.9 | 7157.2 | 9473 |

## ZNZ6_trade
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| fastsend_mbspin_A | 1 | 8,002 | 1.8 | 2.2 | 9.0 | 50.1 | 167.9 | 225.4 | 231 |
| rfs | 1 | 4,022 | 0.6 | 1.2 | 8.9 | 51.5 | 90.3 | 101.9 | 104 |
| mbspin_A | 1 | 5,699 | 3.4 | 5.1 | 14.2 | 53.4 | 90.1 | 101.6 | 106 |
| rfs_pin_A | 2 | 13,304 | 0.5 | 0.9 | 8.5 | 53.6 | 97.8 | 140.7 | 154 |
| mbspin | 2 | 23,331 | 3.4 | 4.7 | 13.5 | 59.1 | 199.2 | 250.9 | 265 |
| fastsend | 2 | 23,015 | 2.3 | 4.0 | 12.2 | 67.6 | 242.8 | 393.4 | 425 |
| p4s_A | 1 | 7,409 | 10.0 | 13.3 | 26.4 | 69.2 | 189.5 | 239.1 | 247 |
| fastsend_mbspin | 4 | 37,474 | 0.9 | 2.1 | 10.8 | 76.1 | 304.1 | 648.8 | 1551 |
| base_A | 1 | 8,336 | 3.9 | 5.6 | 16.1 | 77.8 | 161.3 | 198.4 | 202 |
| base | 4 | 38,632 | 4.2 | 6.6 | 16.8 | 78.4 | 226.1 | 329.0 | 351 |
| rfs_pin | 2 | 15,649 | 0.6 | 1.1 | 11.0 | 79.0 | 199.2 | 1876.6 | 1898 |
| fastsend_A | 1 | 7,842 | 2.6 | 4.7 | 15.4 | 93.5 | 303.2 | 331.2 | 335 |
| p4s | 2 | 16,343 | 9.4 | 13.5 | 29.0 | 101.8 | 247.2 | 805.5 | 817 |
| fsmb_pin | 2 | 21,058 | 0.9 | 1.7 | 15.5 | 190.6 | 1309.4 | 2456.7 | 2482 |

# Per-pass p50 / p99 / p999 (consistency check)

## ESZ6_book
| config | p1 | p2 | x1 | x2 |
|---|---|---|---|---|
| base | 7.5 / 23.7 / 39 | 6.8 / 21.3 / 42 | 6.1 / 21.6 / 114 | 6.2 / 27.3 / 73 |
| fastsend | 3.9 / 21.9 / 998 | 5.0 / 19.4 / 48 | - | - |
| mbspin | 5.2 / 16.4 / 36 | 5.4 / 17.0 / 33 | - | - |
| fastsend_mbspin | 2.5 / 13.3 / 31 | 2.5 / 15.8 / 32 | 2.4 / 13.9 / 27 | 2.6 / 15.3 / 34 |
| p4s | 11.6 / 36.4 / 140 | 12.4 / 35.6 / 78 | - | - |
| base_A | - | 6.6 / 16.9 / 37 | - | - |
| fastsend_A | - | 4.4 / 14.4 / 30 | - | - |
| mbspin_A | - | 4.0 / 13.5 / 26 | - | - |
| fastsend_mbspin_A | - | 2.3 / 11.1 / 23 | - | - |
| p4s_A | - | 10.3 / 25.6 / 50 | - | - |
| fsmb_pin | - | - | 1.6 / 65.0 / 6769 | 1.7 / 31.0 / 135 |
| rfs | - | - | - | 1.6 / 13.8 / 29 |
| rfs_pin | - | - | 1.3 / 43.7 / 320 | 1.3 / 17.6 / 162 |
| rfs_pin_A | - | - | 1.2 / 16.2 / 40 | 1.2 / 16.9 / 45 |

## NQZ6_book
| config | p1 | p2 | x1 | x2 |
|---|---|---|---|---|
| base | 6.8 / 14.0 / 34 | 6.5 / 12.7 / 22 | 6.5 / 12.3 / 21 | 4.9 / 11.4 / 19 |
| fastsend | 4.3 / 10.4 / 155 | 4.8 / 10.3 / 18 | - | - |
| mbspin | 5.3 / 9.8 / 16 | 5.4 / 10.4 / 20 | - | - |
| fastsend_mbspin | 2.1 / 6.9 / 16 | 2.3 / 7.7 / 15 | 2.2 / 7.0 / 14 | 2.3 / 7.0 / 12 |
| p4s | 11.5 / 23.1 / 113 | 10.8 / 22.4 / 595 | - | - |
| base_A | - | 5.0 / 10.4 / 18 | - | - |
| fastsend_A | - | 2.8 / 7.5 / 13 | - | - |
| mbspin_A | - | 4.1 / 8.4 / 13 | - | - |
| fastsend_mbspin_A | - | 2.1 / 5.8 / 9 | - | - |
| p4s_A | - | 11.2 / 20.8 / 31 | - | - |
| fsmb_pin | - | - | 1.4 / 37.8 / 412 | 1.4 / 17.2 / 186 |
| rfs | - | - | - | 1.3 / 6.6 / 13 |
| rfs_pin | - | - | 1.1 / 9.5 / 88 | 1.1 / 7.3 / 74 |
| rfs_pin_A | - | - | 1.0 / 6.1 / 15 | 1.1 / 6.3 / 16 |

## ZNZ6_book
| config | p1 | p2 | x1 | x2 |
|---|---|---|---|---|
| base | 6.2 / 58.7 / 166 | 7.6 / 63.7 / 149 | 7.2 / 65.6 / 118 | 8.7 / 63.2 / 85 |
| fastsend | 4.3 / 51.8 / 155 | 4.8 / 55.2 / 188 | - | - |
| mbspin | 4.6 / 33.7 / 130 | 5.3 / 50.9 / 190 | - | - |
| fastsend_mbspin | 2.4 / 59.7 / 297 | 2.5 / 42.9 / 122 | 2.5 / 107.4 / 476 | 2.2 / 55.2 / 128 |
| p4s | 13.3 / 79.1 / 185 | 13.9 / 97.8 / 278 | - | - |
| base_A | - | 6.6 / 66.0 / 266 | - | - |
| fastsend_A | - | 4.7 / 44.6 / 196 | - | - |
| mbspin_A | - | 5.5 / 46.2 / 85 | - | - |
| fastsend_mbspin_A | - | 2.5 / 46.7 / 109 | - | - |
| p4s_A | - | 13.5 / 58.1 / 164 | - | - |
| fsmb_pin | - | - | 1.9 / 463.4 / 1289 | 1.8 / 210.0 / 765 |
| rfs | - | - | - | 1.4 / 53.9 / 163 |
| rfs_pin | - | - | 1.3 / 99.8 / 338 | 1.3 / 114.7 / 388 |
| rfs_pin_A | - | - | 1.2 / 33.6 / 95 | 1.2 / 40.6 / 105 |

# Latency by ingress qlen (packets queued ahead at MsgBuf), book streams pooled
| config | qlen | share | msgs | p50 | p99 | p999 |
|---|---|---|---|---|---|---|
| base | 0 | 90.93% | 1,858,282 | 6.5 | 21.9 | 64.6 |
| base | 1 | 8.77% | 179,329 | 6.1 | 58.4 | 130.8 |
| base | 2-3 | 0.27% | 5,491 | 8.2 | 118.9 | 552.0 |
| base | 4+ | 0.03% | 584 | 85.6 | 5295.5 | 7156.1 |
| fastsend | 0 | 90.36% | 1,264,134 | 4.8 | 15.3 | 36.9 |
| fastsend | 1 | 9.03% | 126,320 | 4.0 | 36.2 | 67.5 |
| fastsend | 2-3 | 0.47% | 6,643 | 9.3 | 145.3 | 999.5 |
| fastsend | 4+ | 0.14% | 1,940 | 96.3 | 2260.0 | 2985.7 |
| mbspin | 0 | 97.06% | 2,380,449 | 5.3 | 14.3 | 39.9 |
| mbspin | 1 | 2.89% | 70,953 | 5.6 | 47.9 | 157.8 |
| mbspin | 2-3 | 0.03% | 783 | 11.2 | 159.4 | 2569.5 |
| mbspin | 4+ | 0.02% | 411 | 128.7 | 1962.4 | 2036.1 |
| fastsend_mbspin | 0 | 96.72% | 2,708,116 | 2.3 | 11.3 | 34.1 |
| fastsend_mbspin | 1 | 2.98% | 83,574 | 2.8 | 56.9 | 108.4 |
| fastsend_mbspin | 2-3 | 0.18% | 4,924 | 43.2 | 149.5 | 960.0 |
| fastsend_mbspin | 4+ | 0.11% | 3,213 | 112.1 | 1892.8 | 2310.2 |
| p4s | 0 | 90.98% | 1,044,473 | 11.9 | 35.9 | 161.8 |
| p4s | 1 | 8.63% | 99,050 | 11.2 | 54.6 | 298.5 |
| p4s | 2-3 | 0.34% | 3,928 | 14.2 | 270.6 | 8446.5 |
| p4s | 4+ | 0.05% | 628 | 113.4 | 9524.4 | 9648.5 |
| base_A | 0 | 99.26% | 779,822 | 5.9 | 15.2 | 60.8 |
| base_A | 1 | 0.67% | 5,271 | 9.8 | 116.1 | 304.5 |
| base_A | 2-3 | 0.03% | 238 | 32.7 | 901.0 | 1001.2 |
| base_A | 4+ | 0.04% | 289 | 51.1 | 1125.8 | 1126.9 |
| fastsend_A | 0 | 99.05% | 807,936 | 3.3 | 11.5 | 29.0 |
| fastsend_A | 1 | 0.83% | 6,733 | 8.5 | 56.9 | 85.9 |
| fastsend_A | 2-3 | 0.08% | 655 | 50.3 | 131.1 | 380.8 |
| fastsend_A | 4+ | 0.04% | 342 | 166.9 | 311.3 | 341.2 |
| mbspin_A | 0 | 99.56% | 646,107 | 4.1 | 12.7 | 47.1 |
| mbspin_A | 1 | 0.43% | 2,810 | 8.4 | 106.0 | 342.9 |
| mbspin_A | 2-3 | 0.01% | 55 | 15.5 | 325.3 | 325.3 |
| fastsend_mbspin_A | 0 | 99.53% | 938,047 | 2.2 | 8.4 | 28.9 |
| fastsend_mbspin_A | 1 | 0.40% | 3,809 | 8.8 | 117.3 | 145.9 |
| fastsend_mbspin_A | 2-3 | 0.06% | 552 | 52.2 | 127.3 | 359.5 |
| fastsend_mbspin_A | 4+ | 0.01% | 68 | 67.5 | 335.6 | 335.6 |
| p4s_A | 0 | 99.35% | 896,336 | 11.1 | 24.6 | 64.6 |
| p4s_A | 1 | 0.63% | 5,659 | 14.8 | 57.3 | 104.5 |
| p4s_A | 2-3 | 0.01% | 132 | 22.4 | 492.6 | 508.0 |
| p4s_A | 4+ | 0.01% | 118 | 113.1 | 2520.2 | 2553.6 |
| fsmb_pin | 0 | 95.65% | 990,653 | 1.5 | 22.2 | 91.5 |
| fsmb_pin | 1 | 2.34% | 24,187 | 4.2 | 212.5 | 1271.1 |
| fsmb_pin | 2-3 | 0.59% | 6,160 | 28.7 | 675.9 | 2304.1 |
| fsmb_pin | 4+ | 1.42% | 14,713 | 96.3 | 7080.6 | 12846.7 |
| rfs | 0 | 100.00% | 309,958 | 1.4 | 12.4 | 65.5 |
| rfs_pin | 0 | 100.00% | 973,446 | 1.2 | 29.0 | 250.0 |
| rfs_pin_A | 0 | 100.00% | 747,237 | 1.1 | 15.7 | 61.9 |

# Discussion of each setup

Numbers quoted below are from the 2026-10-01 runs in the tables above. They are
front-month book updates, written ES / NQ / ZN, in microseconds. If the tables
are regenerated from new runs, re-check the numbers quoted here.

A "hop" means a message handed to another thread's mailbox. A hop into a
thread that is asleep costs a futex wakeup, about 2-4 us on this feed. The feed
is sparse: about 1 packet per ms on NQ and ZN, 1 per 10 ms on ES. So a
receiving thread is asleep for almost every packet.

## Diagram notation

Each box is an actor. Each column of boxes under a `THREAD` heading runs on
that one OS thread.

```
==send==>        async send: message queued in the receiver's mailbox; the receiver's
                 own thread processes it later. A thread hop.
--fast_send-->   synchronous: the receiver's handler runs immediately, on the
                 caller's thread, under the receiver's lock. No hop.
--call-->        plain virtual function call. No hop.
(sleeps)         the receiving mailbox parks its thread on a condvar when empty;
                 every message pays a futex wakeup.
(spins)          the receiving mailbox busy-polls, so there is no wakeup; it burns
                 a core.
t0 / t1          where the latency clock starts and stops.
```

The book -> LatencyProbe send happens after t1, so it is outside the measured
latency. It is shown only for completeness.

## base (production path)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  ==send==>                          |  ==send==>
        +-----------------+-------------------+
                          v
THREAD MsgBuf  (sleeps)
  [MsgBuf]
    --fast_send-->  [MessageProcessor]   (sequence check, reorder)
    --fast_send-->  [DataDecoder]        (SBE decode)
    --call-->       [handler_if]         (book events)
                          |
                          |  ==send==>  (heap copy of each book event)
                          v
THREAD TachBook  (sleeps)
  [TachBook]  t1
    ==send==>  [LatencyProbe]   (after t1, not measured)

hops between t0 and t1: 2, both into sleeping threads
```

The socket reader hands each packet to MsgBuf (hop 1). MsgBuf, MessageProcessor,
decode and the handler then run inline on the MsgBuf thread. The handler copies
each book event and sends it to TachBook on its own thread (hop 2). Both
receiving threads sleep between packets. This is the path the live recorder
runs.

It is slow at the median (p50 6.6 / 6.3 / 7.3) because both hops pay a wakeup.
The decode and handler work itself is only 0.3-0.9 us. Its tails are among the
better ones with both feeds on (p999 65.6 / 21.9 / 138), for two reasons. Book
work never delays decode: a burst of book updates queues at TachBook, not in
front of the next packet. And no thread busy-polls, so there is little
competition for CPUs. It is the reference point, not a good target.

## fastsend (book inline)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  ==send==>                          |  ==send==>
        +-----------------+-------------------+
                          v
THREAD MsgBuf  (sleeps)  -- also runs the book
  [MsgBuf]
    --fast_send-->  [MessageProcessor]   (sequence check, reorder)
    --fast_send-->  [DataDecoder]        (SBE decode)
    --call-->       [handler_if]         (book events)
    --fast_send-->  [TachBook]  t1       (book update on this same thread)
    ==send==>  [LatencyProbe]   (after t1, not measured)

hops between t0 and t1: 1, into a sleeping thread
```

Same as base, except the handler calls TachBook directly. The book update runs
on the MsgBuf thread, so hop 2 disappears. One hop remains, and it still pays a
wakeup.

The median drops by about 2 us (4.9 / 4.7 / 4.7), which is the cost of hop 2.
The price is in the tail on the books with heavier per-event work. ES p999 goes
from 66 to 80 and ZN from 138 to 179; NQ is unchanged (21). During a burst, the
next packet now waits behind the previous packet's book update. Pass 1 showed
this directly: packets that arrived 2-3 deep in the queue had a p50 of 30 us,
against 9.5 us on base. This is the median-vs-tail trade-off of inlining.

## mbspin (spinning MsgBuf)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  ==send==>                          |  ==send==>
        +-----------------+-------------------+
                          v
THREAD MsgBuf  (SPINS: LockFreeMPSC busy-poll, no wakeup)
  [MsgBuf]
    --fast_send-->  [MessageProcessor]   (sequence check, reorder)
    --fast_send-->  [DataDecoder]        (SBE decode)
    --call-->       [handler_if]         (book events)
                          |
                          |  ==send==>  (heap copy of each book event)
                          v
THREAD TachBook  (sleeps)
  [TachBook]  t1
    ==send==>  [LatencyProbe]   (after t1, not measured)

hops between t0 and t1: 2, the first without a wakeup
```

Same as base, but MsgBuf's mailbox busy-polls instead of sleeping (LockFreeMPSC
consumer spin). Hop 1 still exists, but nothing has to be woken. It costs one
full CPU core per channel.

The socket -> MsgBuf stage drops from 2-4 us to about 0.8 us at the median.
End-to-end p50 improves by about 1-2 us (5.3 / 5.3 / 4.9). The tails are as
good as base or better (p999 34.6 / 17.2 / 153.6), because book work still runs
on its own thread. On an unshared host this would be the safest improvement.
Here the spinning thread is sometimes preempted, which is why its maxima stay
in the milliseconds.

## fastsend_mbspin (both)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  ==send==>                          |  ==send==>
        +-----------------+-------------------+
                          v
THREAD MsgBuf  (SPINS)  -- also runs the book
  [MsgBuf]
    --fast_send-->  [MessageProcessor]   (sequence check, reorder)
    --fast_send-->  [DataDecoder]        (SBE decode)
    --call-->       [handler_if]         (book events)
    --fast_send-->  [TachBook]  t1       (book update on this same thread)
    ==send==>  [LatencyProbe]   (after t1, not measured)

hops between t0 and t1: 1, without a wakeup
```

One hop left, and it does not wake anything: the reader hands off to a spinning
MsgBuf, which runs decode, handler and book inline. It is the best of the
"one hop" designs.

The median is about 3x better than base (2.5 / 2.2 / 2.4), and NQ's tail is
good (p999 15). ZN shows the trade-off at its clearest: p999 290 against 154
for mbspin. ZN packets carry more book events, and they are all processed in
line before the next packet starts. It also spins one core per channel.

## rfs (reader fast_send: zero hops)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  --fast_send-->                     |  --fast_send-->
        +-----------------+-------------------+
                          v   (A and B take turns through MsgBuf's lock)
  [MsgBuf]          (runs on whichever reader thread got there first)
    --fast_send-->  [MessageProcessor]   (sequence check, reorder)
    --fast_send-->  [DataDecoder]        (SBE decode)
    --call-->       [handler_if]         (book events)
    --fast_send-->  [TachBook]  t1       (book update on this same thread)
    ==send==>  [LatencyProbe]   (after t1, not measured)

MsgBuf's own thread is idle. The second copy of each packet (from the other feed)
is dropped by MessageProcessor's sequence check.
hops between t0 and t1: 0. Everything runs on the socket-reader thread.
```

The socket reader calls MsgBuf directly. MsgBuf, MessageProcessor, decode,
handler and the book update all run on the reader thread. t0 and t1 are taken
on the same thread. With feeds A and B, the two reader threads take turns
through MsgBuf's lock. The second one to arrive drops the duplicate packet.

This is the fastest design measured (p50 1.6 / 1.3 / 1.4; p1 0.5-0.6). It had
no packet loss (Onload socket and NIC-ring drop counters all 0) and no gaps.
The exchange-to-t0 measurement shows no extra waiting in the socket buffer
compared with base. Its p999 (28.6 / 12.9 / 162.5) is better than base on ES and
NQ and similar on ZN. The risks: the reader is not reading its socket while it
works, so a heavy burst could build up in the socket buffer. And A and B
contend for one lock. This is one unpinned run on a quiet day; it needs repeats,
including on a high-volume day.

## p4s (parallel decode)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  ==send==>                          |  ==send==>
        +-----------------+-------------------+
                          v
THREAD MsgBuf  (sleeps)
  [MsgBuf]
    --fast_send-->  [MessageProcessor]
                          |
                          |  ==send==>  (copy of the packet, round-robin to worker k)
                          v
THREAD worker k  (x4 per channel, SPINS)
  [DataDecoderActor k]
    --call-->  [DataDecoder] + [RecordingHandler]   (decode, record handler calls)
                          |
                          |  ==send==>  (packet bytes + recorded calls)
                          v
THREAD HandlerIfActor  (SPINS)
  [HandlerIfActor]   (puts packets back in dispatch order)
    --call-->  [handler_if]   (replays the recorded calls)
    ==send==>  DecodeDone back to MessageProcessor's own thread (off the latency path)
                          |
                          |  ==send==>  (heap copy of each book event)
                          v
THREAD TachBook  (sleeps)
  [TachBook]  t1
    ==send==>  [LatencyProbe]   (after t1, not measured)

hops between t0 and t1: 4 (MsgBuf asleep, worker spins, handler spins, TachBook asleep)
```

MessageProcessor copies each packet and sends it round-robin to 4 decode
workers per channel. Each worker decodes into a recording of handler calls. A
single HandlerIfActor replays the recordings into the handler in packet order,
then sends to the book. Workers and HandlerIfActor busy-poll. Hops: MsgBuf,
worker, HandlerIfActor, book.

It is the slowest design (p50 12.3 / 11.0 / 13.7) and has some of the worst
tails (p999 85 / 347 / 265). CME packets carry about one message each, so there
is nothing to split across workers. Each packet just pays for two extra hops,
two copies, and a heap-allocated recording per handler call (worker decode p99
on ZN is 9.1 us, against 2.0 for serial decode). Fifteen extra threads must
also be on a CPU at the right moment. In 61 of 84 ten-second windows a hot
thread was waiting for a CPU while a message took over 100 us. And one stalled
worker holds up every packet behind it.

## fsmb_pin and rfs_pin (pinned, not isolated)

```
fsmb_pin = the fastsend_mbspin diagram, threads pinned (ES / NQ / ZN, NUMA node 2):
  SocketReader A  -> cpu 17 / 19 / 21
  SocketReader B  -> cpu 49 / 51 / 53   (SMT sibling of A: same physical core)
  MsgBuf (spins; runs decode + book) -> cpu 16 / 18 / 20 (a core to itself)
  idle actors -> cpu 22, TachBook threads -> cpu 54, all other threads -> off node 2

rfs_pin = the rfs diagram, threads pinned:
  SocketReader A  -> cpu 16 / 18 / 20   (a core to itself)
  SocketReader B  -> cpu 17 / 19 / 21   (a core to itself)
  MsgBuf thread (idle) -> cpu 22, TachBook threads -> cpu 54, all other threads -> off node 2

CPUs 17-22 also handle the storage controller's interrupts (mpi3mr0).
Nothing is isolated.
```

These are fastsend_mbspin and rfs with every busy thread pinned to fixed CPUs
on NUMA node 2. All other kaspr threads are kept off node 2. In fsmb_pin, sock
A and sock B of a channel share one core as SMT siblings, and MsgBuf has a core
to itself. In rfs_pin, each socket reader has a core to itself. The CPUs are not
isolated from the kernel.

The medians are excellent (fsmb_pin 1.7 / 1.4 / 1.9; rfs_pin 1.3 / 1.1 / 1.3).
The tails are the worst measured (fsmb_pin p999 739 / 312 / 1283; rfs_pin 254 /
84 / 362). The pinned threads waited 22-51 ms per 10 s in the run queue. Those
CPUs also handle the storage controller's interrupts (mpi3mr0), and a pinned
thread cannot move away when kernel or interrupt work lands on it; an unpinned
one simply migrates. Pinning only pays when the CPUs are isolated (isolcpus /
nohz_full, IRQ affinity moved off them). That needs a reboot and was not
tested.

## *_A variants (feed B off)

```
THREAD sock A
  [SocketReader A]  t0
        |  ==send==>   (or --fast_send--> in rfs_pin_A)
        v
  ... rest exactly as in the named config ...

SocketReader B is never started: one copy of each packet, no A/B arbitration,
one fewer busy-polling thread per channel.
```

Same as the named config, but socket reader B is never started. Each channel
reads feed A only, so there is no arbitration between A and B.

Medians are about the same as with both feeds. Tails are almost always shorter:

| config | with A+B (p999) | A only (p999) |
|---|---|---|
| fastsend_mbspin | 30 / 15 / 290 | 23 / 9 / 109 |
| rfs_pin | 254 / 84 / 362 | 43 / 16 / 101 |
| p4s | 85 / 347 / 265 | 50 / 31 / 164 |

Two reasons: one fewer busy-polling thread per channel competing for CPUs, and,
in rfs, no A/B contention on MsgBuf's lock. The cost is resilience. A packet
lost on feed A becomes a gap and a recovery instead of being filled from B.
None happened in 7 A-only runs (about 55 minutes), but that is a short and quiet
sample.

# How to reproduce these numbers

All code, configs and scripts are on kaspar-hft branch `md-latency-experiments`.
That branch is experimental and is not merged to main. The full guide is in
`../md_median_vs_tail_draft.md`, section "How to reproduce". In short:

1. **Host.** Live CME MDP3 multicast on two interfaces, with OpenOnload
   installed. Stop any other kaspr reading the same groups: runs are solo. Use a
   universe of contracts that are live on the run date
   (`kaspr/config/universe.csv`).
2. **Build.**
   - Rebuild all the libraries with `./build.sh`, then kaspr with
     `./build.sh -C kaspr/src USE_TACHBOOK=1`.
   - Rebuild everything after any header change: a stale library cost one run
     here.
   - If the link fails, set `BOOST_PATH=/usr/local/boost188`.
3. **Configs.** Copy `configs/<label>/` to `kaspr/config_<label>/` for each
   label in the tables. The pinned configs hard-code CPU ids for this host
   (EPYC 9374F, node 2 = CPUs 16-23 and 48-55); remap them for another machine.
4. **Run each config**, 8-10 minutes, interleaving configs and rotating their
   order between passes. `run_matrix3.sh` in this directory does all of this:

   ```bash
   KHPROJ=$PWD OUTDIR=$OUT kaspr/run_probe.sh --no-restart -t 480 -c ../config_<label>/md_perf.ini
   ```

   - Pinned configs: run under `taskset -c 0-15,24-47,56-63`.
   - Meanwhile: `schedsample.py <pid> $OUT/probe.out $OUT/sched.csv 10`.
   - After the window: save `onload_stackdump lots` for the process's stacks to
     `$OUT/onload.txt`, then `kill -9` the probe (known ZMQ shutdown hang;
     samples are already flushed) and copy the newest `kaspr/kaspr_log.*.log`
     to `$OUT/kaspr.log`.
   - Name each run directory `<pass>_<label>`, e.g. `p1_base` or `x2_rfs_pin_A`.
5. **Analyse.** `R=<parent of the run directories> bash paper_all.sh`
   regenerates this file, plus `stages.md`, `sched.md` and `health.md`. The
   first 120 s of each run are dropped. Percentiles are over every message of
   the run directories with that label.

